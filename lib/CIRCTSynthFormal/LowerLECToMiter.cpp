//===- LowerLECToMiter.cpp - Build FRAIG LEC miters ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CIRCTSynthFormal/MiterUtils.h"
#include "CIRCTSynthFormal/CIRCTSynthFormalPasses.h"
#include "circt/Dialect/Comb/CombDialect.h"
#include "circt/Dialect/Comb/CombOps.h"
#include "circt/Dialect/HW/HWDialect.h"
#include "circt/Dialect/HW/HWOps.h"
#include "circt/Dialect/Seq/SeqDialect.h"
#include "circt/Dialect/Seq/SeqOps.h"
#include "circt/Dialect/Synth/SynthDialect.h"
#include "circt/Dialect/Synth/SynthOps.h"
#include "circt/Dialect/Verif/VerifDialect.h"
#include "circt/Dialect/Verif/VerifOps.h"
#include "circt/Support/BackedgeBuilder.h"
#include "llvm/ADT/SmallPtrSet.h"

using namespace mlir;
using namespace circt;
using namespace circt::fraig_lec;

namespace circt {
namespace fraig_lec {
#define GEN_PASS_DEF_LOWERLECTOMITER
#include "CIRCTSynthFormal/CIRCTSynthFormalPasses.h.inc"
} // namespace fraig_lec
} // namespace circt

namespace {

struct LowerLECToMiterPass
    : public circt::fraig_lec::impl::LowerLECToMiterBase<LowerLECToMiterPass> {
  using circt::fraig_lec::impl::LowerLECToMiterBase<
      LowerLECToMiterPass>::LowerLECToMiterBase;

  void runOnOperation() final;

private:
  FailureOr<hw::HWModuleOp> lower(verif::LogicEquivalenceCheckingOp lecOp,
                                  unsigned index);
};

} // namespace

static LogicalResult
collectRegistersInBlock(SymbolTable &symbolTable, Block &block,
                        OpBuilder &builder, StringRef pathPrefix,
                        DenseMap<StringAttr, Type> &regs,
                        SmallVector<StringAttr> *order,
                        llvm::SmallPtrSetImpl<Operation *> &moduleStack) {
  for (Operation &op : block.without_terminator()) {
    if (auto inst = dyn_cast<hw::InstanceOp>(op)) {
      auto targetModule = resolveInstanceModule(symbolTable, inst);
      if (failed(targetModule))
        return failure();
      if (!moduleStack.insert(targetModule->getOperation()).second) {
        inst.emitError() << "recursive instance hierarchy is not supported";
        return failure();
      }
      std::string childPath =
          appendInstancePath(pathPrefix, inst.getInstanceName());
      if (failed(collectRegistersInBlock(symbolTable,
                                         *targetModule->getBodyBlock(), builder,
                                         childPath, regs, order, moduleStack)))
        return failure();
      moduleStack.erase(targetModule->getOperation());
      continue;
    }

    if (!isSeqOp(&op))
      continue;
    if (isSupportedStatelessSeqOp(&op))
      continue;
    if (!isSupportedRegister(&op)) {
      op.emitError() << "unsupported sequential op in verif.lec region";
      return failure();
    }
    if (auto firreg = dyn_cast<seq::FirRegOp>(op))
      if (firreg.getIsAsync()) {
        op.emitError() << "async-reset firreg is not supported";
        return failure();
      }

    StringAttr name = getHierarchicalRegisterName(&op, builder, pathPrefix);
    if (!name) {
      op.emitError() << "registers in sequential LEC regions must be named";
      return failure();
    }
    if (!regs.insert({name, getRegisterDataType(&op)}).second) {
      op.emitError() << "duplicate register name '" << name.getValue() << "'";
      return failure();
    }
    if (order)
      order->push_back(name);
  }
  return success();
}

static FailureOr<SmallVector<MatchedRegister>>
collectMatchedRegisters(SymbolTable &symbolTable, Block &firstBlock,
                        Block &secondBlock, OpBuilder &builder) {
  DenseMap<StringAttr, Type> firstRegs;
  DenseMap<StringAttr, Type> secondRegs;
  SmallVector<StringAttr> firstOrder;
  llvm::SmallPtrSet<Operation *, 8> moduleStack;

  if (failed(collectRegistersInBlock(symbolTable, firstBlock, builder,
                                     /*pathPrefix=*/"", firstRegs, &firstOrder,
                                     moduleStack)) ||
      failed(collectRegistersInBlock(symbolTable, secondBlock, builder,
                                     /*pathPrefix=*/"", secondRegs, nullptr,
                                     moduleStack)))
    return failure();

  if (firstRegs.size() != secondRegs.size()) {
    firstBlock.getParentOp()->emitError()
        << "LEC regions must have matching named register sets";
    return failure();
  }

  SmallVector<MatchedRegister> matched;
  matched.reserve(firstOrder.size());
  for (StringAttr name : firstOrder) {
    auto firstIt = firstRegs.find(name);
    auto secondIt = secondRegs.find(name);
    if (secondIt == secondRegs.end()) {
      firstBlock.getParentOp()->emitError()
          << "no matching register named '" << name.getValue()
          << "' in second LEC region";
      return failure();
    }
    if (firstIt->second != secondIt->second) {
      firstBlock.getParentOp()->emitError()
          << "matched register '" << name.getValue()
          << "' has different types: " << firstIt->second << " vs "
          << secondIt->second;
      return failure();
    }
    matched.push_back({name, firstIt->second});
  }

  return matched;
}

static StringAttr getUniqueSymbolName(SymbolTable &symbolTable,
                                      OpBuilder &builder, StringRef baseName) {
  std::string name = baseName.str();
  unsigned suffix = 0;
  while (symbolTable.lookup(name))
    name = (baseName + "_" + Twine(++suffix)).str();
  return builder.getStringAttr(name);
}

static FailureOr<SmallVector<Value>>
cloneCircuitBody(OpBuilder &builder, Block &srcBlock, ValueRange miterInputs,
                 StringRef side, StringRef pathPrefix) {
  IRMapping mapping;
  for (auto [srcArg, input] : llvm::zip(srcBlock.getArguments(), miterInputs))
    mapping.map(srcArg, input);

  BackedgeBuilder backedgeBuilder(builder, srcBlock.getParentOp()->getLoc());
  DenseMap<Value, Backedge> backedges;
  for (Operation &op : srcBlock.without_terminator())
    for (Value result : op.getResults()) {
      Backedge backedge = backedgeBuilder.get(result.getType());
      mapping.map(result, backedge);
      backedges.insert({result, backedge});
    }

  auto resolveBackedges = [&](Operation &source, Operation *cloned) {
    for (auto [sourceResult, clonedResult] :
         llvm::zip(source.getResults(), cloned->getResults())) {
      auto it = backedges.find(sourceResult);
      if (it != backedges.end())
        it->second.setValue(clonedResult);
      mapping.map(sourceResult, clonedResult);
    }
  };

  for (Operation &op : srcBlock.without_terminator()) {
    if (isSupportedStatelessSeqOp(&op)) {
      Operation *cloned = builder.clone(op, mapping);
      resolveBackedges(op, cloned);
      continue;
    }

    if (isSeqOp(&op)) {
      if (!isSupportedRegister(&op)) {
        op.emitError() << "unsupported sequential op in verif.lec region";
        return failure();
      }
      Operation *cloned = builder.clone(op, mapping);
      resolveBackedges(op, cloned);
      setMiterLocalAttrs(cloned, builder, side, pathPrefix);
      continue;
    }

    Operation *cloned = builder.clone(op, mapping);
    resolveBackedges(op, cloned);
    if (isa<hw::InstanceOp>(cloned))
      setMiterLocalAttrs(cloned, builder, side, pathPrefix);
  }

  if (failed(backedgeBuilder.clearOrEmitError()))
    return failure();

  SmallVector<Value> results;
  if (auto yield = dyn_cast<verif::YieldOp>(srcBlock.getTerminator())) {
    for (Value operand : yield.getInputs())
      results.push_back(mapping.lookupOrDefault(operand));
    return results;
  }
  if (auto output = dyn_cast<hw::OutputOp>(srcBlock.getTerminator())) {
    for (Value operand : output.getOutputs())
      results.push_back(mapping.lookupOrDefault(operand));
    return results;
  }
  srcBlock.getTerminator()->emitError()
      << "unsupported terminator while cloning miter region";
  return failure();
}

FailureOr<hw::HWModuleOp>
LowerLECToMiterPass::lower(verif::LogicEquivalenceCheckingOp lecOp,
                           unsigned index) {
  if (lecOp.getIsProven()) {
    lecOp.emitError() << "LEC result values are not supported by this miter "
                         "lowering; use resultless verif.lec";
    return failure();
  }

  Block &firstBlock = lecOp.getFirstCircuit().front();
  Block &secondBlock = lecOp.getSecondCircuit().front();
  auto firstYield = cast<verif::YieldOp>(firstBlock.getTerminator());
  auto secondYield = cast<verif::YieldOp>(secondBlock.getTerminator());
  if (firstBlock.getNumArguments() != secondBlock.getNumArguments() ||
      firstYield.getInputs().size() != secondYield.getInputs().size()) {
    lecOp.emitError() << "LEC regions must have matching input and output "
                         "counts before miter lowering";
    return failure();
  }

  OpBuilder moduleBuilder(getOperation().getBodyRegion());
  moduleBuilder.setInsertionPointToEnd(getOperation().getBody());
  Location loc = lecOp.getLoc();

  SymbolTable symbolTable(getOperation());
  auto matchedRegs = collectMatchedRegisters(symbolTable, firstBlock,
                                             secondBlock, moduleBuilder);
  if (failed(matchedRegs))
    return failure();

  SmallVector<hw::PortInfo> ports;
  ports.reserve(firstBlock.getNumArguments() + matchedRegs->size() * 2 + 1);
  unsigned inputIndex = 0;
  for (auto [argIndex, arg] : llvm::enumerate(firstBlock.getArguments())) {
    if (arg.getType().isInteger(0)) {
      lecOp.emitError() << "zero-width inputs are not supported";
      return failure();
    }
    hw::PortInfo port;
    port.name = moduleBuilder.getStringAttr("in" + Twine(argIndex));
    port.type = arg.getType();
    port.dir = hw::ModulePort::Direction::Input;
    port.argNum = inputIndex++;
    ports.push_back(port);
  }
  for (auto &reg : *matchedRegs) {
    static constexpr llvm::StringLiteral sides[] = {"lhs", "rhs"};
    for (StringRef side : sides) {
      hw::PortInfo port;
      port.name = moduleBuilder.getStringAttr(getStatePortName(reg.name, side));
      port.type = reg.type;
      port.dir = hw::ModulePort::Direction::Input;
      port.argNum = inputIndex++;
      ports.push_back(port);
    }
  }

  hw::PortInfo failPort;
  failPort.name = moduleBuilder.getStringAttr("fail");
  failPort.type = moduleBuilder.getI1Type();
  failPort.dir = hw::ModulePort::Direction::Output;
  failPort.argNum = 0;
  ports.push_back(failPort);

  std::string baseName =
      index == 0 ? "lec_miter" : ("lec_miter_" + Twine(index)).str();
  StringAttr moduleName =
      getUniqueSymbolName(symbolTable, moduleBuilder, baseName);
  auto miter = hw::HWModuleOp::create(moduleBuilder, loc, moduleName, ports);
  miter->setAttr(kMiterAttrName, moduleBuilder.getUnitAttr());
  miter->setAttr(kMiterKindAttrName, moduleBuilder.getStringAttr("lec"));

  Block *body = miter.getBodyBlock();
  if (!body->empty())
    body->back().erase();

  OpBuilder bodyBuilder = OpBuilder::atBlockEnd(body);
  ValueRange originalInputs =
      body->getArguments().take_front(firstBlock.getNumArguments());
  auto firstOutputs = cloneCircuitBody(bodyBuilder, firstBlock, originalInputs,
                                       "lhs", /*pathPrefix=*/"");
  if (failed(firstOutputs))
    return failure();
  auto secondOutputs = cloneCircuitBody(
      bodyBuilder, secondBlock, originalInputs, "rhs", /*pathPrefix=*/"");
  if (failed(secondOutputs))
    return failure();

  SmallVector<Value> mismatches;
  mismatches.reserve(firstOutputs->size());
  for (auto [lhs, rhs] : llvm::zip(*firstOutputs, *secondOutputs)) {
    auto mismatch = createOutputMismatch(bodyBuilder, loc, lhs, rhs);
    if (failed(mismatch)) {
      lecOp.emitError() << "only matching integer output types are supported";
      return failure();
    }
    mismatches.push_back(*mismatch);
  }

  Value fail;
  if (mismatches.empty()) {
    fail = hw::ConstantOp::create(bodyBuilder, loc, bodyBuilder.getI1Type(), 0);
  } else if (mismatches.size() == 1) {
    fail = mismatches.front();
  } else {
    fail = comb::OrOp::create(bodyBuilder, loc, mismatches, true);
  }
  hw::OutputOp::create(bodyBuilder, loc, fail);
  for (auto [index, mismatch] : llvm::enumerate(mismatches))
    appendCheckOutput(miter, (Twine("output ") + Twine(index)).str(), mismatch);
  lecOp.erase();
  return miter;
}

void LowerLECToMiterPass::runOnOperation() {
  SmallVector<verif::LogicEquivalenceCheckingOp> lecOps;
  getOperation().walk(
      [&](verif::LogicEquivalenceCheckingOp op) { lecOps.push_back(op); });
  if (lecOps.empty()) {
    return;
  }

  for (auto [index, lecOp] : llvm::enumerate(lecOps))
    if (failed(lower(lecOp, index)))
      return signalPassFailure();
}
