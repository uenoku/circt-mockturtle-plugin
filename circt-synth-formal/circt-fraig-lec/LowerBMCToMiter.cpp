//===- LowerBMCToMiter.cpp - Build FRAIG BMC miters ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MiterUtils.h"
#include "Passes.h"
#include "circt/Dialect/Comb/CombDialect.h"
#include "circt/Dialect/Comb/CombOps.h"
#include "circt/Dialect/HW/HWDialect.h"
#include "circt/Dialect/HW/HWOps.h"
#include "circt/Dialect/HW/HWTypes.h"
#include "circt/Dialect/Seq/SeqDialect.h"
#include "circt/Dialect/Seq/SeqOps.h"
#include "circt/Dialect/Verif/VerifDialect.h"
#include "circt/Dialect/Verif/VerifOps.h"
#include "llvm/ADT/SmallPtrSet.h"
#include <limits>

using namespace mlir;
using namespace circt;
using namespace circt::fraig_lec;

static constexpr StringLiteral kBmcKInductionAttrName = "fraig_lec.k_induction";
static constexpr StringLiteral kBmcRecurrenceAttrName = "fraig_lec.recurrence";

namespace circt {
namespace fraig_lec {
#define GEN_PASS_DEF_LOWERBMCTOMITER
#include "Passes.h.inc"
} // namespace fraig_lec
} // namespace circt

namespace {

struct LowerBMCToMiterPass
    : public circt::fraig_lec::impl::LowerBMCToMiterBase<LowerBMCToMiterPass> {
  using circt::fraig_lec::impl::LowerBMCToMiterBase<
      LowerBMCToMiterPass>::LowerBMCToMiterBase;

  void runOnOperation() final;

private:
  enum class Mode { Base, InductionStep, Recurrence };

  FailureOr<hw::HWModuleOp> lower(SymbolTable &symbolTable,
                                  verif::BoundedModelCheckingOp bmcOp,
                                  unsigned index, Mode mode);
};

struct UnrolledProperty {
  std::string label;
  Value failure;
};

struct CloneResult {
  SmallVector<Value> yields;
  SmallVector<Value> assumptions;
  SmallVector<UnrolledProperty> assertions;
};

} // namespace

static StringAttr getUniqueSymbolName(SymbolTable &symbolTable,
                                      OpBuilder &builder, StringRef baseName) {
  std::string name = baseName.str();
  unsigned suffix = 0;
  while (symbolTable.lookup(name))
    name = (baseName + "_" + Twine(++suffix)).str();
  return builder.getStringAttr(name);
}

static Value getConstant(OpBuilder &builder, Location loc, Type type,
                         const APInt &value) {
  auto intType = dyn_cast<IntegerType>(type);
  assert(intType && "constant type must be integer");
  return hw::ConstantOp::create(builder, loc,
                                value.zextOrTrunc(intType.getWidth()));
}

static Value getTrue(OpBuilder &builder, Location loc) {
  return hw::ConstantOp::create(builder, loc, APInt(1, 1));
}

static Value getFalse(OpBuilder &builder, Location loc) {
  return hw::ConstantOp::create(builder, loc, APInt(1, 0));
}

static bool isSupportedBMCValueType(Type type) {
  return type.isInteger() || hw::type_isa<hw::ArrayType>(type);
}

static Value createNot(OpBuilder &builder, Location loc, Value value) {
  return builder.createOrFold<comb::XorOp>(loc, value, getTrue(builder, loc));
}

static Value createAnd(OpBuilder &builder, Location loc, Value lhs, Value rhs) {
  return builder.createOrFold<comb::AndOp>(loc, ValueRange{lhs, rhs}, true);
}

static Value createOr(OpBuilder &builder, Location loc, Value lhs, Value rhs) {
  return builder.createOrFold<comb::OrOp>(loc, ValueRange{lhs, rhs}, true);
}

static FailureOr<Value> createEq(OpBuilder &builder, Location loc, Value lhs,
                                 Value rhs) {
  if (lhs.getType().isInteger())
    return builder.createOrFold<comb::ICmpOp>(loc, comb::ICmpPredicate::eq, lhs,
                                              rhs);

  if (hw::type_isa<hw::ArrayType>(lhs.getType())) {
    int64_t width = hw::getBitWidth(lhs.getType());
    if (width <= 0 || width > std::numeric_limits<unsigned>::max())
      return failure();
    auto intType = builder.getIntegerType(width);
    lhs = hw::BitcastOp::create(builder, loc, intType, lhs);
    rhs = hw::BitcastOp::create(builder, loc, intType, rhs);
    return builder.createOrFold<comb::ICmpOp>(loc, comb::ICmpPredicate::eq, lhs,
                                              rhs);
  }

  return failure();
}

static FailureOr<Value> lookupLocal(Block &sourceBlock, IRMapping &mapping,
                                    Value value, Operation *user) {
  Value mapped = lookupMapped(mapping, value);
  if (mapped == value) {
    if (auto arg = dyn_cast<BlockArgument>(value)) {
      if (arg.getOwner() == &sourceBlock)
        return user->emitError() << "unmapped BMC region block argument";
    } else if (value.getDefiningOp() &&
               value.getDefiningOp()->getBlock() == &sourceBlock) {
      return user->emitError() << "unmapped BMC region value";
    }
  }
  return mapped;
}

static FailureOr<Value> getAssertLikeHolds(Block &sourceBlock,
                                           OpBuilder &builder,
                                           IRMapping &mapping, Operation *op,
                                           Value property, Value enable) {
  if (!property.getType().isInteger(1))
    return op->emitError()
           << "only single-bit BMC assert/assume properties are supported";

  auto mappedProperty = lookupLocal(sourceBlock, mapping, property, op);
  if (failed(mappedProperty))
    return failure();

  if (!enable)
    return *mappedProperty;

  auto mappedEnable = lookupLocal(sourceBlock, mapping, enable, op);
  if (failed(mappedEnable))
    return failure();
  if (!mappedEnable->getType().isInteger(1))
    return op->emitError()
           << "only single-bit BMC assert/assume enables are supported";

  Location loc = op->getLoc();
  return createOr(builder, loc, createNot(builder, loc, *mappedEnable),
                  *mappedProperty);
}

static std::string getAssertLabel(verif::AssertOp assertOp, unsigned stepIndex,
                                  unsigned assertIndex) {
  if (auto label = assertOp.getLabel())
    return (Twine("step ") + Twine(stepIndex) + " assertion " + *label).str();
  return (Twine("step ") + Twine(stepIndex) + " assertion " +
          Twine(assertIndex))
      .str();
}

static LogicalResult
ensureNoNestedProperties(SymbolTable &symbolTable,
                         verif::BoundedModelCheckingOp op) {
  llvm::SmallPtrSet<Operation *, 8> visited;
  SmallVector<Operation *> worklist;
  for (auto inst : op.getCircuit().getOps<hw::InstanceOp>())
    if (auto *target = symbolTable.lookup(inst.getReferencedModuleName()))
      worklist.push_back(target);

  while (!worklist.empty()) {
    Operation *op = worklist.pop_back_val();
    if (!visited.insert(op).second)
      continue;
    if (op->getParentOfType<verif::BoundedModelCheckingOp>())
      continue;

    bool hasProperty = false;
    op->walk([&](Operation *nested) {
      if (isa<verif::AssertOp, verif::AssumeOp, verif::CoverOp,
              verif::ClockedAssertOp, verif::ClockedAssumeOp,
              verif::ClockedCoverOp>(nested))
        hasProperty = true;
    });
    if (hasProperty)
      return op->emitError()
             << "assertions and assumptions inside BMC instances must be "
                "inlined before FRAIG BMC lowering";

    auto module = dyn_cast<hw::HWModuleOp>(op);
    if (!module)
      continue;
    for (auto inst : module.getOps<hw::InstanceOp>())
      if (auto *target = symbolTable.lookup(inst.getReferencedModuleName()))
        worklist.push_back(target);
  }
  return success();
}

static FailureOr<CloneResult>
cloneBMCBlock(OpBuilder &builder, Block &sourceBlock, ValueRange inputs,
              unsigned stepIndex, bool collectAssertions,
              bool collectAssumptions) {
  if (sourceBlock.getNumArguments() != inputs.size())
    return sourceBlock.getParentOp()->emitError()
           << "BMC clone input count mismatch";

  IRMapping mapping;
  for (auto [arg, input] : llvm::zip(sourceBlock.getArguments(), inputs))
    mapping.map(arg, input);

  CloneResult result;
  unsigned assertIndex = 0;
  for (Operation &op : sourceBlock.without_terminator()) {
    if (auto toClock = dyn_cast<seq::ToClockOp>(op)) {
      auto input = lookupLocal(sourceBlock, mapping, toClock.getInput(), &op);
      if (failed(input))
        return failure();
      mapping.map(toClock.getResult(), *input);
      continue;
    }
    if (auto fromClock = dyn_cast<seq::FromClockOp>(op)) {
      auto input = lookupLocal(sourceBlock, mapping, fromClock.getInput(), &op);
      if (failed(input))
        return failure();
      mapping.map(fromClock.getResult(), *input);
      continue;
    }

    if (auto assertOp = dyn_cast<verif::AssertOp>(op)) {
      if (!collectAssertions)
        continue;
      auto holds =
          getAssertLikeHolds(sourceBlock, builder, mapping, &op,
                             assertOp.getProperty(), assertOp.getEnable());
      if (failed(holds))
        return failure();
      result.assertions.push_back(
          {getAssertLabel(assertOp, stepIndex, assertIndex++),
           createNot(builder, op.getLoc(), *holds)});
      continue;
    }
    if (auto assumeOp = dyn_cast<verif::AssumeOp>(op)) {
      if (!collectAssumptions)
        continue;
      auto holds =
          getAssertLikeHolds(sourceBlock, builder, mapping, &op,
                             assumeOp.getProperty(), assumeOp.getEnable());
      if (failed(holds))
        return failure();
      result.assumptions.push_back(*holds);
      continue;
    }
    if (isa<verif::CoverOp, verif::ClockedAssertOp, verif::ClockedAssumeOp,
            verif::ClockedCoverOp>(op))
      return op.emitError()
             << "only unclocked verif.assert and verif.assume are supported "
                "by FRAIG BMC lowering";
    if (isSeqOp(&op))
      return op.emitError()
             << "only seq.to_clock and seq.from_clock are supported by FRAIG "
                "BMC lowering";
    for (Type type : op.getOperandTypes())
      if (isa<seq::ClockType>(type))
        return op.emitError()
               << "operation with clock operand is not supported by FRAIG BMC "
                  "lowering";
    for (Type type : op.getResultTypes())
      if (isa<seq::ClockType>(type))
        return op.emitError()
               << "operation with clock result is not supported by FRAIG BMC "
                  "lowering";

    for (Value operand : op.getOperands())
      if (failed(lookupLocal(sourceBlock, mapping, operand, &op)))
        return failure();

    Operation *cloned = builder.clone(op, mapping);
    for (auto [sourceResult, clonedResult] :
         llvm::zip(op.getResults(), cloned->getResults()))
      mapping.map(sourceResult, clonedResult);
    if (auto inst = dyn_cast<hw::InstanceOp>(cloned))
      setMiterLocalAttrs(inst, builder, "bmc",
                         (Twine("step") + Twine(stepIndex)).str());
  }

  auto yield = dyn_cast<verif::YieldOp>(sourceBlock.getTerminator());
  if (!yield)
    return sourceBlock.getTerminator()->emitError()
           << "expected verif.yield in BMC region";

  for (Value operand : yield.getInputs()) {
    auto mapped = lookupLocal(sourceBlock, mapping, operand, yield);
    if (failed(mapped))
      return failure();
    result.yields.push_back(*mapped);
  }
  return result;
}

static Value buildAssumptionGuard(OpBuilder &builder, Location loc,
                                  ValueRange assumptions) {
  if (assumptions.empty())
    return getTrue(builder, loc);
  if (assumptions.size() == 1)
    return assumptions.front();
  return comb::AndOp::create(builder, loc, assumptions, true);
}

static FailureOr<Value> createInitialRegisterValue(OpBuilder &builder,
                                                   Location loc, Type type,
                                                   Attribute initialValue,
                                                   Value unitInitialInput) {
  if (isa<UnitAttr>(initialValue))
    return unitInitialInput;
  auto integer = dyn_cast<IntegerAttr>(initialValue);
  if (!integer)
    return failure();
  if (!isa<IntegerType>(type))
    return failure();
  return getConstant(builder, loc, type, integer.getValue());
}

static FailureOr<Value>
buildLoopFreePathCheck(OpBuilder &builder, Location loc,
                       ArrayRef<SmallVector<Value>> stateSnapshots) {
  if (stateSnapshots.size() < 2 || stateSnapshots.front().empty())
    return getFalse(builder, loc);

  Value loopFree = getTrue(builder, loc);
  for (unsigned i = 0, e = stateSnapshots.size(); i != e; ++i) {
    for (unsigned j = i + 1; j != e; ++j) {
      Value statesEqual = getTrue(builder, loc);
      for (auto [lhs, rhs] : llvm::zip(stateSnapshots[i], stateSnapshots[j])) {
        auto equal = createEq(builder, loc, lhs, rhs);
        if (failed(equal))
          return failure();
        statesEqual = createAnd(builder, loc, statesEqual, *equal);
      }
      loopFree = createAnd(builder, loc, loopFree,
                           createNot(builder, loc, statesEqual));
    }
  }
  return loopFree;
}

FailureOr<hw::HWModuleOp>
LowerBMCToMiterPass::lower(SymbolTable &symbolTable,
                           verif::BoundedModelCheckingOp bmcOp, unsigned index,
                           Mode mode) {
  if (!bmcOp->use_empty()) {
    bmcOp.emitError() << "BMC result values are not supported by this miter "
                         "lowering; use resultless top-level verif.bmc";
    return failure();
  }

  if (failed(ensureNoNestedProperties(symbolTable, bmcOp)))
    return failure();

  Block &initBlock = bmcOp.getInit().front();
  Block &loopBlock = bmcOp.getLoop().front();
  Block &circuitBlock = bmcOp.getCircuit().front();
  unsigned numRegs = bmcOp.getNumRegs();
  if (numRegs > circuitBlock.getNumArguments())
    return bmcOp.emitError() << "BMC num_regs exceeds circuit arguments";
  unsigned regStartIndex = circuitBlock.getNumArguments() - numRegs;

  OpBuilder moduleBuilder(getOperation().getBodyRegion());
  moduleBuilder.setInsertionPointToEnd(getOperation().getBody());
  Location loc = bmcOp.getLoc();

  SmallVector<hw::PortInfo> ports;
  unsigned inputIndex = 0;
  SmallVector<Attribute> counterexampleNames;
  DenseMap<unsigned, SmallVector<StringAttr>> frameInputPorts;
  SmallVector<std::string> inputNames;
  bool hasInputNames = false;
  if (auto names = bmcOp->getAttrOfType<ArrayAttr>("fraig_lec.input_names")) {
    hasInputNames = true;
    for (auto [index, attr] : llvm::enumerate(names)) {
      auto name = dyn_cast<StringAttr>(attr);
      if (!name)
        return bmcOp.emitError()
               << "BMC input name #" << index << " is not a string attribute";
      inputNames.push_back(name.getValue().str());
    }
  }

  SmallVector<std::string> stateNames;
  bool hasStateNames = false;
  if (auto names = bmcOp->getAttrOfType<ArrayAttr>("fraig_lec.state_names")) {
    hasStateNames = true;
    if (names.size() != numRegs)
      return bmcOp.emitError()
             << "BMC state name count does not match num_regs";
    for (auto [index, attr] : llvm::enumerate(names)) {
      auto name = dyn_cast<StringAttr>(attr);
      if (!name)
        return bmcOp.emitError()
               << "BMC state name #" << index << " is not a string attribute";
      stateNames.push_back(name.getValue().str());
    }
  }

  unsigned numSteps = bmcOp.getBound() + (mode == Mode::InductionStep ? 1 : 0);
  for (unsigned step = 0; step != numSteps; ++step) {
    unsigned primaryInputIndex = 0;
    for (auto [argIndex, arg] : llvm::enumerate(circuitBlock.getArguments())) {
      if (isa<seq::ClockType>(arg.getType()) || argIndex >= regStartIndex)
        continue;
      if (!isSupportedBMCValueType(arg.getType())) {
        bmcOp.emitError()
            << "only integer and HW array non-clock BMC circuit inputs are "
               "supported";
        return failure();
      }
      hw::PortInfo port;
      port.name = moduleBuilder.getStringAttr("step" + Twine(step) + "_in" +
                                              Twine(argIndex));
      port.type = arg.getType();
      port.dir = hw::ModulePort::Direction::Input;
      port.argNum = inputIndex++;
      frameInputPorts[step].push_back(port.name);
      std::string displayName;
      if (!hasInputNames) {
        displayName = port.name.getValue().str();
      } else {
        if (primaryInputIndex >= inputNames.size())
          return bmcOp.emitError()
                 << "BMC input name count does not match circuit inputs";
        displayName =
            (Twine("step") + Twine(step) + "_" + inputNames[primaryInputIndex])
                .str();
      }
      counterexampleNames.push_back(moduleBuilder.getStringAttr(displayName));
      ports.push_back(port);
      ++primaryInputIndex;
    }
    if (hasInputNames && primaryInputIndex != inputNames.size())
      return bmcOp.emitError()
             << "BMC input name count does not match circuit inputs";
  }

  SmallVector<StringAttr> unitInitialRegPorts(numRegs);
  for (auto [regIndex, initialValue] :
       llvm::enumerate(bmcOp.getInitialValues()))
    if (mode == Mode::InductionStep || isa<UnitAttr>(initialValue)) {
      Type type = circuitBlock.getArgument(regStartIndex + regIndex).getType();
      if (!isSupportedBMCValueType(type)) {
        bmcOp.emitError()
            << "unit-initialized BMC registers must be integer or HW array";
        return failure();
      }
      hw::PortInfo port;
      port.name = moduleBuilder.getStringAttr(
          (Twine(mode == Mode::InductionStep ? "state_reg" : "init_reg") +
           Twine(regIndex))
              .str());
      port.type = type;
      port.dir = hw::ModulePort::Direction::Input;
      port.argNum = inputIndex++;
      unitInitialRegPorts[regIndex] = port.name;
      std::string displayName = port.name.getValue().str();
      if (hasStateNames)
        displayName = (Twine(mode == Mode::InductionStep ? "state_" : "init_") +
                       stateNames[regIndex])
                          .str();
      counterexampleNames.push_back(moduleBuilder.getStringAttr(displayName));
      ports.push_back(port);
    }

  hw::PortInfo failPort;
  failPort.name = moduleBuilder.getStringAttr("fail");
  failPort.type = moduleBuilder.getI1Type();
  failPort.dir = hw::ModulePort::Direction::Output;
  failPort.argNum = 0;
  ports.push_back(failPort);

  std::string baseName;
  if (mode == Mode::InductionStep)
    baseName = index == 0 ? "bmc_induction_miter"
                          : ("bmc_induction_miter_" + Twine(index)).str();
  else if (mode == Mode::Recurrence)
    baseName = index == 0 ? "bmc_recurrence_miter"
                          : ("bmc_recurrence_miter_" + Twine(index)).str();
  else
    baseName = index == 0 ? "bmc_miter" : ("bmc_miter_" + Twine(index)).str();
  StringAttr moduleName =
      getUniqueSymbolName(symbolTable, moduleBuilder, baseName);
  auto miter = hw::HWModuleOp::create(moduleBuilder, loc, moduleName, ports);
  miter->setAttr(kMiterAttrName, moduleBuilder.getUnitAttr());
  miter->setAttr(kCounterexampleNamesAttrName,
                 moduleBuilder.getArrayAttr(counterexampleNames));
  miter->setAttr(
      kMiterKindAttrName,
      moduleBuilder.getStringAttr(mode == Mode::InductionStep ? "bmc_induction"
                                  : mode == Mode::Recurrence  ? "bmc_recurrence"
                                                              : "bmc"));

  Block *body = miter.getBodyBlock();
  if (!body->empty())
    body->back().erase();
  OpBuilder bodyBuilder = OpBuilder::atBlockEnd(body);

  auto init = cloneBMCBlock(bodyBuilder, initBlock, ValueRange{},
                            /*stepIndex=*/0, /*collectAssertions=*/false,
                            /*collectAssumptions=*/false);
  if (failed(init))
    return failure();

  SmallVector<Type> circuitArgTypes(circuitBlock.getArgumentTypes());
  SmallVector<unsigned> clockIndexes;
  for (auto [index, type] : llvm::enumerate(circuitArgTypes))
    if (isa<seq::ClockType>(type))
      clockIndexes.push_back(index);
  if (init->yields.size() < clockIndexes.size())
    return bmcOp.emitError()
           << "BMC init yields fewer values than circuit clock inputs";

  SmallVector<Value> clocks;
  clocks.reserve(clockIndexes.size());
  for (unsigned i = 0, e = clockIndexes.size(); i != e; ++i)
    clocks.push_back(init->yields[i]);

  SmallVector<Value> loopState;
  for (Value value : ArrayRef(init->yields).drop_front(clockIndexes.size()))
    loopState.push_back(value);

  SmallVector<Value> registers;
  registers.reserve(numRegs);
  for (unsigned regIndex = 0; regIndex != numRegs; ++regIndex) {
    Type type = circuitBlock.getArgument(regStartIndex + regIndex).getType();
    Value unitInput;
    if (unitInitialRegPorts[regIndex])
      unitInput = lookupMiterInput(miter, unitInitialRegPorts[regIndex]);
    auto initial = mode == Mode::InductionStep
                       ? FailureOr<Value>(unitInput)
                       : createInitialRegisterValue(
                             bodyBuilder, loc, type,
                             bmcOp.getInitialValues()[regIndex], unitInput);
    if (failed(initial)) {
      bmcOp.emitError()
          << "BMC initial register values must be integer or unit attributes";
      return failure();
    }
    registers.push_back(*initial);
  }

  Value fail = getFalse(bodyBuilder, loc);
  SmallVector<UnrolledProperty> checks;
  Value pathGuard = getTrue(bodyBuilder, loc);
  Value inductionGuard = getTrue(bodyBuilder, loc);
  SmallVector<SmallVector<Value>> stateSnapshots;
  auto ignoreAssertionsUntil =
      bmcOp->getAttrOfType<IntegerAttr>("ignore_asserts_until");
  uint64_t ignoreUntil = ignoreAssertionsUntil
                             ? ignoreAssertionsUntil.getValue().getZExtValue()
                             : 0;
  bool updateRegsEveryStep = bmcOp->hasAttr("fraig_lec.update_regs_every_step");

  for (unsigned step = 0; step != numSteps; ++step) {
    if (mode == Mode::Recurrence)
      stateSnapshots.push_back(registers);

    SmallVector<Value> circuitInputs(circuitBlock.getNumArguments());
    unsigned clockInputIndex = 0;
    unsigned freshInputIndex = 0;
    for (auto [argIndex, arg] : llvm::enumerate(circuitBlock.getArguments())) {
      if (isa<seq::ClockType>(arg.getType())) {
        circuitInputs[argIndex] = clocks[clockInputIndex++];
        continue;
      }
      if (argIndex >= regStartIndex) {
        circuitInputs[argIndex] = registers[argIndex - regStartIndex];
        continue;
      }
      auto portName = frameInputPorts[step][freshInputIndex++];
      circuitInputs[argIndex] = lookupMiterInput(miter, portName);
    }

    auto circuit = cloneBMCBlock(
        bodyBuilder, circuitBlock, circuitInputs, step,
        /*collectAssertions=*/mode != Mode::Recurrence && step >= ignoreUntil,
        /*collectAssumptions=*/true);
    if (failed(circuit))
      return failure();
    if (circuit->yields.size() < numRegs)
      return bmcOp.emitError()
             << "BMC circuit yields fewer values than num_regs";

    Value assumptions =
        buildAssumptionGuard(bodyBuilder, loc, circuit->assumptions);
    pathGuard = createAnd(bodyBuilder, loc, pathGuard, assumptions);
    if (mode == Mode::InductionStep)
      inductionGuard = createAnd(bodyBuilder, loc, inductionGuard, assumptions);
    for (auto &assertion : circuit->assertions) {
      if (mode == Mode::InductionStep && step + 1 != numSteps) {
        Value assertionHolds = createNot(
            bodyBuilder, assertion.failure.getLoc(), assertion.failure);
        inductionGuard = createAnd(bodyBuilder, assertion.failure.getLoc(),
                                   inductionGuard, assertionHolds);
        continue;
      }

      Value guard = mode == Mode::InductionStep ? inductionGuard : pathGuard;
      assertion.failure = createAnd(bodyBuilder, assertion.failure.getLoc(),
                                    guard, assertion.failure);
      fail = createOr(bodyBuilder, assertion.failure.getLoc(), fail,
                      assertion.failure);
      checks.push_back(assertion);
    }

    SmallVector<Value> loopInputs;
    loopInputs.append(clocks);
    loopInputs.append(loopState);
    auto loop = cloneBMCBlock(bodyBuilder, loopBlock, loopInputs, step,
                              /*collectAssertions=*/false,
                              /*collectAssumptions=*/false);
    if (failed(loop))
      return failure();
    if (loop->yields.size() != loopInputs.size())
      return bmcOp.emitError()
             << "BMC loop yield count must match loop input count";

    SmallVector<Value> newClocks;
    for (unsigned i = 0, e = clocks.size(); i != e; ++i)
      newClocks.push_back(loop->yields[i]);

    SmallVector<Value> nextRegisters(circuit->yields.end() - numRegs,
                                     circuit->yields.end());
    if (clocks.size() == 1 && !updateRegsEveryStep) {
      Value oldClockLow = createNot(bodyBuilder, loc, clocks.front());
      Value isPosedge =
          createAnd(bodyBuilder, loc, oldClockLow, newClocks.front());
      for (auto [reg, next] : llvm::zip(registers, nextRegisters))
        next = bodyBuilder.createOrFold<comb::MuxOp>(loc, isPosedge, next, reg);
    } else if (clocks.size() > 1) {
      bmcOp.emitError() << "BMC lowering currently supports at most one clock";
      return failure();
    }
    registers = std::move(nextRegisters);

    clocks = std::move(newClocks);
    loopState.clear();
    for (Value value : ArrayRef(loop->yields).drop_front(clocks.size()))
      loopState.push_back(value);
  }

  if (mode == Mode::Recurrence) {
    stateSnapshots.push_back(registers);
    auto loopFreePath =
        buildLoopFreePathCheck(bodyBuilder, loc, stateSnapshots);
    if (failed(loopFreePath))
      return bmcOp.emitError()
             << "failed to build recurrence check for BMC state";
    fail = createAnd(bodyBuilder, loc, pathGuard, *loopFreePath);
    checks.push_back({"loop-free path", fail});
  }

  hw::OutputOp::create(bodyBuilder, loc, fail);
  for (auto &check : checks)
    appendCheckOutput(miter, check.label, check.failure);

  return miter;
}

void LowerBMCToMiterPass::runOnOperation() {
  SmallVector<verif::BoundedModelCheckingOp> bmcOps;
  getOperation().walk(
      [&](verif::BoundedModelCheckingOp op) { bmcOps.push_back(op); });
  if (bmcOps.empty())
    return;

  SymbolTable symbolTable(getOperation());
  for (auto [index, bmcOp] : llvm::enumerate(bmcOps)) {
    if (failed(lower(symbolTable, bmcOp, index, Mode::Base)))
      return signalPassFailure();
    if (bmcOp->hasAttr(kBmcKInductionAttrName))
      if (failed(lower(symbolTable, bmcOp, index, Mode::InductionStep)))
        return signalPassFailure();
    if (bmcOp->hasAttr(kBmcRecurrenceAttrName))
      if (failed(lower(symbolTable, bmcOp, index, Mode::Recurrence)))
        return signalPassFailure();
    bmcOp.erase();
  }
}
