//===- InlineInstanceLayer.cpp - Gradual miter inlining ---------*- C++ -*-===//
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
#include "circt/Dialect/HW/HWOps.h"
#include "circt/Dialect/Seq/SeqDialect.h"
#include "circt/Dialect/Seq/SeqOps.h"
#include "circt/Support/BackedgeBuilder.h"

using namespace mlir;
using namespace circt;
using namespace circt::fraig_lec;

namespace circt {
namespace fraig_lec {
#define GEN_PASS_DEF_INLINEINSTANCELAYER
#include "Passes.h.inc"
} // namespace fraig_lec
} // namespace circt

namespace {

struct InlineInstanceLayerPass
    : public circt::fraig_lec::impl::InlineInstanceLayerBase<
          InlineInstanceLayerPass> {
  using circt::fraig_lec::impl::InlineInstanceLayerBase<
      InlineInstanceLayerPass>::InlineInstanceLayerBase;
  void runOnOperation() final;
};
} // namespace

static FailureOr<SmallVector<Value>>
cloneOneLayer(OpBuilder &builder, Block &srcBlock, ValueRange inputs,
              StringRef side, StringRef pathPrefix) {
  IRMapping mapping;
  for (auto [arg, input] : llvm::zip(srcBlock.getArguments(), inputs))
    mapping.map(arg, input);

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
        op.emitError() << "unsupported sequential op in miter";
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

  SmallVector<Value> outputs;
  auto terminator = dyn_cast<hw::OutputOp>(srcBlock.getTerminator());
  if (!terminator) {
    srcBlock.getTerminator()->emitError()
        << "expected hw.output while inlining instance";
    return failure();
  }
  for (Value output : terminator.getOutputs())
    outputs.push_back(mapping.lookupOrDefault(output));
  return outputs;
}

static LogicalResult inlineInstance(SymbolTable &symbolTable,
                                    hw::InstanceOp inst) {
  auto side = inst->getAttrOfType<StringAttr>(kSideAttrName);
  auto path = inst->getAttrOfType<StringAttr>(kPathAttrName);
  if (!side || !path)
    return inst.emitError() << "miter-local instance is missing metadata";

  auto targetModule = resolveInstanceModule(symbolTable, inst);
  if (failed(targetModule))
    return failure();

  SmallVector<Value> inputs(inst.getInputs());
  std::string childPath =
      appendInstancePath(path.getValue(), inst.getInstanceName());
  OpBuilder builder(inst);
  auto outputs = cloneOneLayer(builder, *targetModule->getBodyBlock(), inputs,
                               side.getValue(), childPath);
  if (failed(outputs))
    return failure();

  if (inst->getNumResults() != outputs->size())
    return inst.emitError() << "inlined instance result count mismatch";
  for (auto [result, output] : llvm::zip(inst->getResults(), *outputs))
    result.replaceAllUsesWith(output);
  inst.erase();
  return success();
}

void InlineInstanceLayerPass::runOnOperation() {
  SymbolTable symbolTable(getOperation());
  SmallVector<hw::InstanceOp> instances;
  for (auto miter : getOperation().getOps<hw::HWModuleOp>()) {
    if (!isMiter(miter))
      continue;
    for (auto inst : miter.getOps<hw::InstanceOp>())
      instances.push_back(inst);
  }

  for (auto inst : instances)
    if (failed(inlineInstance(symbolTable, inst)))
      return signalPassFailure();
}
