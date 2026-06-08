//===- MatchRegisters.cpp - Match exposed miter registers -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MiterUtils.h"
#include "Passes.h"
#include "circt/Dialect/Comb/CombDialect.h"
#include "circt/Dialect/HW/HWDialect.h"
#include "circt/Dialect/Seq/SeqOps.h"
#include "circt/Dialect/Synth/SynthDialect.h"
#include "circt/Dialect/Synth/SynthOps.h"

using namespace mlir;
using namespace circt;
using namespace circt::fraig_lec;

namespace circt {
namespace fraig_lec {
#define GEN_PASS_DEF_MATCHREGISTERS
#include "Passes.h.inc"
} // namespace fraig_lec
} // namespace circt

namespace {

struct MatchRegistersPass
    : public circt::fraig_lec::impl::MatchRegistersBase<MatchRegistersPass> {
  using circt::fraig_lec::impl::MatchRegistersBase<
      MatchRegistersPass>::MatchRegistersBase;
  void runOnOperation() final;
};
} // namespace

static LogicalResult collectExposedRegisters(
    hw::HWModuleOp miter, DenseMap<StringAttr, Operation *> &lhsRegs,
    DenseMap<StringAttr, Operation *> &rhsRegs, OpBuilder &builder) {
  for (Operation &op : miter.getBodyBlock()->without_terminator()) {
    if (!isSeqOp(&op))
      continue;
    if (isSupportedStatelessSeqOp(&op))
      continue;
    if (!isSupportedRegister(&op))
      return op.emitError() << "unsupported sequential op in miter";
    if (auto firreg = dyn_cast<seq::FirRegOp>(op))
      if (firreg.getIsAsync())
        return op.emitError() << "async-reset firreg is not supported";

    auto side = op.getAttrOfType<StringAttr>(kSideAttrName);
    if (!side)
      return op.emitError() << "miter-local register is missing side metadata";
    if (side.getValue() != "lhs" && side.getValue() != "rhs")
      return op.emitError()
             << "miter-local register side '" << side.getValue()
             << "' is not supported by register matching; BMC inputs must "
                "externalize registers before unrolling";
    StringAttr name = getMiterLocalRegisterName(&op, builder);
    if (!name)
      return op.emitError() << "miter-local register must be named";

    auto &regs = side.getValue() == "lhs" ? lhsRegs : rhsRegs;
    if (!regs.insert({name, &op}).second)
      return op.emitError()
             << "duplicate exposed register name '" << name.getValue()
             << "' on " << side.getValue() << " side";
  }
  return success();
}

static Operation *getEarlierInBlock(Operation *a, Operation *b) {
  return b->isBeforeInBlock(a) ? b : a;
}

static LogicalResult matchRegisterPair(hw::HWModuleOp miter, StringAttr name,
                                       Operation *lhsReg, Operation *rhsReg) {
  if (getRegisterDataType(lhsReg) != getRegisterDataType(rhsReg))
    return lhsReg->emitError() << "matched register '" << name.getValue()
                               << "' has different exposed types";

  Value lhsState = lookupMiterInput(miter, getStatePortName(name, "lhs"));
  Value rhsState = lookupMiterInput(miter, getStatePortName(name, "rhs"));
  if (!lhsState || !rhsState)
    return miter.emitError() << "missing state inputs for matched register '"
                             << name.getValue() << "'";

  OpBuilder builder(getEarlierInBlock(lhsReg, rhsReg));
  Value stateChoice = synth::ChoiceOp::create(builder, lhsReg->getLoc(),
                                              getRegisterDataType(lhsReg),
                                              ValueRange{lhsState, rhsState})
                          .getResult();

  IRMapping lhsMapping;
  IRMapping rhsMapping;
  lhsMapping.map(lhsReg->getResult(0), stateChoice);
  rhsMapping.map(rhsReg->getResult(0), stateChoice);

  auto output = cast<hw::OutputOp>(miter.getBodyBlock()->getTerminator());
  builder.setInsertionPoint(output);
  Value lhsNext =
      createEffectiveNextValue(builder, lhsReg, lhsMapping, stateChoice);
  Value rhsNext =
      createEffectiveNextValue(builder, rhsReg, rhsMapping, stateChoice);
  auto mismatch =
      createOutputMismatch(builder, lhsReg->getLoc(), lhsNext, rhsNext);
  if (failed(mismatch))
    return lhsReg->emitError() << "matched register '" << name.getValue()
                               << "' has unsupported next-state type";

  lhsReg->getResult(0).replaceAllUsesWith(stateChoice);
  rhsReg->getResult(0).replaceAllUsesWith(stateChoice);
  lhsReg->erase();
  rhsReg->erase();
  appendCheckOutput(
      miter, (Twine("register ") + name.getValue() + " next-state").str(),
      *mismatch);
  return appendToFail(miter, *mismatch);
}

void MatchRegistersPass::runOnOperation() {
  for (auto miter : getOperation().getOps<hw::HWModuleOp>()) {
    if (!isMiter(miter))
      continue;

    OpBuilder builder(miter.getContext());
    DenseMap<StringAttr, Operation *> lhsRegs;
    DenseMap<StringAttr, Operation *> rhsRegs;
    if (failed(collectExposedRegisters(miter, lhsRegs, rhsRegs, builder)))
      return signalPassFailure();

    SmallVector<std::tuple<StringAttr, Operation *, Operation *>> pairs;
    for (auto [name, lhsReg] : lhsRegs)
      if (auto rhsIt = rhsRegs.find(name); rhsIt != rhsRegs.end())
        pairs.push_back({name, lhsReg, rhsIt->second});
    llvm::sort(pairs, [](const auto &lhs, const auto &rhs) {
      return std::get<0>(lhs).getValue() < std::get<0>(rhs).getValue();
    });

    for (auto [name, lhsReg, rhsReg] : pairs)
      if (failed(matchRegisterPair(miter, name, lhsReg, rhsReg)))
        return signalPassFailure();
  }
}
