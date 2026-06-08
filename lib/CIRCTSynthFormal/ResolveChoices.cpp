//===- ResolveChoices.cpp - Resolve FRAIG choices --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CIRCTSynthFormal/CIRCTSynthFormalPasses.h"
#include "circt/Dialect/Synth/SynthDialect.h"
#include "circt/Dialect/Synth/SynthOps.h"

using namespace mlir;
using namespace circt;
using namespace circt::fraig_lec;

namespace circt {
namespace fraig_lec {
#define GEN_PASS_DEF_RESOLVECHOICES
#include "CIRCTSynthFormal/CIRCTSynthFormalPasses.h.inc"
} // namespace fraig_lec
} // namespace circt

namespace {

struct ResolveChoicesPass
    : public circt::fraig_lec::impl::ResolveChoicesBase<ResolveChoicesPass> {
  using circt::fraig_lec::impl::ResolveChoicesBase<
      ResolveChoicesPass>::ResolveChoicesBase;
  void runOnOperation() final;
};
} // namespace

void ResolveChoicesPass::runOnOperation() {
  getOperation().walk([](synth::ChoiceOp choice) {
    choice.replaceAllUsesWith(choice.getInputs().front());
  });
  getOperation().walk([](synth::ChoiceOp choice) {
    if (choice.use_empty())
      choice.erase();
  });
}
