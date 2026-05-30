//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef CIRCT_MOCKTURTLE_CIRCTMOCKTURTLEPASSES_H
#define CIRCT_MOCKTURTLE_CIRCTMOCKTURTLEPASSES_H

#include "circt/Dialect/Comb/CombDialect.h"
#include "circt/Dialect/HW/HWDialect.h"
#include "circt/Dialect/Synth/SynthDialect.h"
#include "mlir/Pass/Pass.h"

namespace circt {
namespace mockturtle_plugin {

#define GEN_PASS_DECL
#include "CIRCTMockturtle/CIRCTMockturtlePasses.h.inc"

#define GEN_PASS_REGISTRATION
#include "CIRCTMockturtle/CIRCTMockturtlePasses.h.inc"

} // namespace mockturtle_plugin
} // namespace circt

#endif // CIRCT_MOCKTURTLE_CIRCTMOCKTURTLEPASSES_H
