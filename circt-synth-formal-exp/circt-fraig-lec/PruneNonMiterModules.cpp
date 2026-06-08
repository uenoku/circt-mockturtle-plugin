//===- PruneNonMiterModules.cpp - Drop helper modules ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MiterUtils.h"
#include "Passes.h"
#include "circt/Dialect/HW/HWDialect.h"
#include "circt/Dialect/HW/HWOps.h"

using namespace mlir;
using namespace circt;
using namespace circt::fraig_lec;

namespace circt {
namespace fraig_lec {
#define GEN_PASS_DEF_PRUNENONMITERMODULES
#include "Passes.h.inc"
} // namespace fraig_lec
} // namespace circt

namespace {

struct PruneNonMiterModulesPass
    : public circt::fraig_lec::impl::PruneNonMiterModulesBase<
          PruneNonMiterModulesPass> {
  using circt::fraig_lec::impl::PruneNonMiterModulesBase<
      PruneNonMiterModulesPass>::PruneNonMiterModulesBase;
  void runOnOperation() final;
};
} // namespace

void PruneNonMiterModulesPass::runOnOperation() {
  SmallVector<hw::HWModuleOp> modulesToErase;
  for (auto module : getOperation().getOps<hw::HWModuleOp>())
    if (!isMiter(module))
      modulesToErase.push_back(module);
  for (auto module : modulesToErase)
    module.erase();
}
