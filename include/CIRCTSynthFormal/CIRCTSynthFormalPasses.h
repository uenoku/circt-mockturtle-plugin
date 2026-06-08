//===- Passes.h - SynthFormal pass entry points ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef CIRCT_SYNTHFORMAL_PASSES_H
#define CIRCT_SYNTHFORMAL_PASSES_H

#include "mlir/Pass/Pass.h"

namespace circt {
namespace fraig_lec {

#define GEN_PASS_DECL
#include "CIRCTSynthFormal/CIRCTSynthFormalPasses.h.inc"

#define GEN_PASS_REGISTRATION
#include "CIRCTSynthFormal/CIRCTSynthFormalPasses.h.inc"

} // namespace fraig_lec
} // namespace circt

#endif // CIRCT_SYNTHFORMAL_PASSES_H
