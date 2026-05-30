//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef CIRCT_MOCKTURTLE_PASSES
#define CIRCT_MOCKTURTLE_PASSES

include "mlir/Pass/PassBase.td"

def MockturtleAIGStats
    : Pass<"synth-mockturtle-aig-stats", "hw::HWModuleOp"> {
  let summary = "Build a mockturtle AIG view of synth.aig.and_inv operations";
  let description = [{
    This pass is the first out-of-tree mockturtle driver pass. It walks a
    hardware module, constructs a mockturtle AIG network from
    `synth.aig.and_inv` operations, and annotates the module with the number of
    mockturtle gates created.
  }];
  let dependentDialects = [
    "::circt::hw::HWDialect",
    "::circt::synth::SynthDialect"
  ];
  let options = [
    Option<"annotate", "annotate", "bool", "true",
           "Annotate each hw.module with mockturtle.aig_gates">
  ];
}

#endif // CIRCT_MOCKTURTLE_PASSES
