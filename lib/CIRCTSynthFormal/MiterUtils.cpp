//===- MiterUtils.cpp - FRAIG LEC shared utilities --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CIRCTSynthFormal/MiterUtils.h"
#include "circt/Dialect/Comb/CombOps.h"
#include "circt/Dialect/Seq/SeqDialect.h"
#include "circt/Dialect/Seq/SeqOps.h"
#include "llvm/ADT/TypeSwitch.h"
#include "llvm/Support/MathExtras.h"

using namespace mlir;
using namespace circt;
using namespace circt::fraig_lec;

bool circt::fraig_lec::isMiter(hw::HWModuleOp module) {
  return module->hasAttr(kMiterAttrName);
}

bool circt::fraig_lec::isSupportedRegister(Operation *op) {
  return isa<seq::CompRegOp, seq::CompRegClockEnabledOp, seq::FirRegOp>(op);
}

bool circt::fraig_lec::isSupportedStatelessSeqOp(Operation *op) {
  return isa<seq::ToClockOp, seq::FromClockOp>(op);
}

bool circt::fraig_lec::isSeqOp(Operation *op) {
  return op->getName().getDialectNamespace() ==
         seq::SeqDialect::getDialectNamespace();
}

std::optional<StringRef> circt::fraig_lec::getRegisterName(Operation *op) {
  return TypeSwitch<Operation *, std::optional<StringRef>>(op)
      .Case<seq::CompRegOp, seq::CompRegClockEnabledOp>([](auto reg) {
        auto name = reg.getName();
        if (!name || name->empty())
          return std::optional<StringRef>();
        return std::optional<StringRef>(*name);
      })
      .Case<seq::FirRegOp>([](seq::FirRegOp reg) {
        if (reg.getName().empty())
          return std::optional<StringRef>();
        return std::optional<StringRef>(reg.getName());
      })
      .Default([](Operation *) { return std::optional<StringRef>(); });
}

Type circt::fraig_lec::getRegisterDataType(Operation *op) {
  return op->getResult(0).getType();
}

StringAttr circt::fraig_lec::getHierarchicalRegisterName(Operation *op,
                                                         OpBuilder &builder,
                                                         StringRef pathPrefix) {
  auto name = getRegisterName(op);
  if (!name)
    return {};
  if (pathPrefix.empty())
    return builder.getStringAttr(*name);
  return builder.getStringAttr((Twine(pathPrefix) + "/" + *name).str());
}

StringAttr circt::fraig_lec::getMiterLocalRegisterName(Operation *op,
                                                       OpBuilder &builder) {
  auto pathAttr = op->getAttrOfType<StringAttr>(kPathAttrName);
  StringRef path = pathAttr ? pathAttr.getValue() : StringRef();
  return getHierarchicalRegisterName(op, builder, path);
}

std::string circt::fraig_lec::appendInstancePath(StringRef pathPrefix,
                                                 StringRef instName) {
  if (pathPrefix.empty())
    return instName.str();
  return (Twine(pathPrefix) + "/" + instName).str();
}

std::string circt::fraig_lec::getStatePortName(StringAttr regName,
                                               StringRef side) {
  return (Twine(regName.getValue()) + "_" + side + "_state").str();
}

Value circt::fraig_lec::lookupMapped(IRMapping &mapping, Value value) {
  return mapping.lookupOrDefault(value);
}

FailureOr<hw::HWModuleOp>
circt::fraig_lec::resolveInstanceModule(SymbolTable &symbolTable,
                                        hw::InstanceOp inst) {
  if (!inst.getParameters().empty()) {
    inst.emitError() << "parameterized instances are not supported by "
                        "miter-local instance flattening";
    return failure();
  }

  Operation *target = symbolTable.lookup(inst.getReferencedModuleName());
  auto targetModule = dyn_cast_or_null<hw::HWModuleOp>(target);
  if (!targetModule) {
    inst.emitError() << "only hw.module instances can be flattened";
    return failure();
  }
  return targetModule;
}

Value circt::fraig_lec::createEffectiveNextValue(OpBuilder &builder,
                                                 Operation *op,
                                                 IRMapping &mapping,
                                                 Value state) {
  return TypeSwitch<Operation *, Value>(op)
      .Case<seq::CompRegOp>([&](seq::CompRegOp reg) {
        Value next = lookupMapped(mapping, reg.getInput());
        if (Value reset = reg.getReset()) {
          Value resetValue = lookupMapped(mapping, reg.getResetValue());
          next = builder.createOrFold<comb::MuxOp>(
              op->getLoc(), lookupMapped(mapping, reset), resetValue, next);
        }
        return next;
      })
      .Case<seq::CompRegClockEnabledOp>([&](seq::CompRegClockEnabledOp reg) {
        Value input = lookupMapped(mapping, reg.getInput());
        Value enable = lookupMapped(mapping, reg.getClockEnable());
        Value next = builder.createOrFold<comb::MuxOp>(op->getLoc(), enable,
                                                       input, state);
        if (Value reset = reg.getReset()) {
          Value resetValue = lookupMapped(mapping, reg.getResetValue());
          next = builder.createOrFold<comb::MuxOp>(
              op->getLoc(), lookupMapped(mapping, reset), resetValue, next);
        }
        return next;
      })
      .Case<seq::FirRegOp>([&](seq::FirRegOp reg) {
        assert(!reg.getIsAsync() && "async resets are rejected during collect");
        Value next = lookupMapped(mapping, reg.getNext());
        if (Value reset = reg.getReset()) {
          Value resetValue = lookupMapped(mapping, reg.getResetValue());
          next = builder.createOrFold<comb::MuxOp>(
              op->getLoc(), lookupMapped(mapping, reset), resetValue, next);
        }
        return next;
      });
}

FailureOr<Value> circt::fraig_lec::createOutputMismatch(OpBuilder &builder,
                                                        Location loc, Value lhs,
                                                        Value rhs) {
  if (lhs.getType() != rhs.getType())
    return failure();
  if (lhs.getType().isInteger())
    return builder.createOrFold<comb::ICmpOp>(loc, comb::ICmpPredicate::ne, lhs,
                                              rhs);

  auto arrayType = hw::type_dyn_cast<hw::ArrayType>(lhs.getType());
  if (!arrayType)
    return failure();

  SmallVector<Value> mismatches;
  mismatches.reserve(arrayType.getNumElements());
  unsigned indexWidth =
      std::max<unsigned>(1, llvm::Log2_64_Ceil(arrayType.getNumElements()));
  auto indexType = builder.getIntegerType(indexWidth);
  for (uint64_t index = 0, e = arrayType.getNumElements(); index != e;
       ++index) {
    Value indexValue = hw::ConstantOp::create(builder, loc, indexType, index);
    Value lhsElement = hw::ArrayGetOp::create(builder, loc, lhs, indexValue);
    Value rhsElement = hw::ArrayGetOp::create(builder, loc, rhs, indexValue);
    auto mismatch = createOutputMismatch(builder, loc, lhsElement, rhsElement);
    if (failed(mismatch))
      return failure();
    mismatches.push_back(*mismatch);
  }

  if (mismatches.empty())
    return hw::ConstantOp::create(builder, loc, builder.getI1Type(), 0)
        .getResult();
  if (mismatches.size() == 1)
    return mismatches.front();
  return comb::OrOp::create(builder, loc, mismatches, true).getResult();
}

Value circt::fraig_lec::lookupMiterInput(hw::HWModuleOp miter,
                                         StringRef portName) {
  Block *body = miter.getBodyBlock();
  for (auto port : miter.getPortList())
    if (port.isInput() && port.name.getValue() == portName)
      return body->getArgument(port.argNum);
  return {};
}

LogicalResult circt::fraig_lec::appendToFail(hw::HWModuleOp miter,
                                             Value mismatch) {
  auto output = dyn_cast<hw::OutputOp>(miter.getBodyBlock()->getTerminator());
  if (!output || output.getOutputs().empty())
    return miter.emitError() << "expected at least one fail output";

  OpBuilder builder(output);
  SmallVector<Value> outputs(output.getOutputs());
  Value oldFail = outputs.front();
  Value newFail = builder.createOrFold<comb::OrOp>(
      mismatch.getLoc(), ValueRange{oldFail, mismatch}, true);
  outputs.front() = newFail;
  hw::OutputOp::create(builder, output.getLoc(), outputs);
  output.erase();
  return success();
}

void circt::fraig_lec::appendCheckOutput(hw::HWModuleOp miter, StringRef label,
                                         Value mismatch) {
  SmallVector<Attribute> checkNames;
  if (auto existing = miter->getAttrOfType<ArrayAttr>(kCheckNamesAttrName))
    llvm::append_range(checkNames, existing);
  checkNames.push_back(StringAttr::get(miter.getContext(), label));
  miter->setAttr(kCheckNamesAttrName,
                 ArrayAttr::get(miter.getContext(), checkNames));

  miter.appendOutput("check" + Twine(checkNames.size() - 1), mismatch);
}

void circt::fraig_lec::setMiterLocalAttrs(Operation *op, OpBuilder &builder,
                                          StringRef side,
                                          StringRef pathPrefix) {
  op->setAttr(kSideAttrName, builder.getStringAttr(side));
  op->setAttr(kPathAttrName, builder.getStringAttr(pathPrefix));
}
