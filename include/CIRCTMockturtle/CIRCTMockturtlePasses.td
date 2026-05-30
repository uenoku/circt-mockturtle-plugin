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

class MockturtlePassBase<string name, string op> : Pass<name, op> {
  let dependentDialects = [
    "::circt::comb::CombDialect",
    "::circt::hw::HWDialect",
    "::circt::synth::SynthDialect"
  ];
}

def MockturtleAIGStats
    : MockturtlePassBase<"synth-mockturtle-aig-stats", "hw::HWModuleOp"> {
  let summary = "Build a mockturtle AIG view of synth.aig.and_inv operations";
  let description = [{
    This pass is the first out-of-tree mockturtle driver pass. It walks a
    hardware module, constructs a mockturtle AIG network from
    `synth.aig.and_inv` operations, and annotates the module with the number of
    mockturtle gates created.
  }];
  let options = [
    Option<"annotate", "annotate", "bool", "true",
           "Annotate each hw.module with mockturtle.aig_gates">
  ];
}

def MockturtleRefactor
    : MockturtlePassBase<"synth-mockturtle-refactor", "hw::HWModuleOp"> {
  let summary = "Run mockturtle SOP refactoring";
  let description = [{
    This pass exports `synth.aig.and_inv` and `synth.majority` regions to
    mockturtle networks, runs mockturtle refactoring, and imports the rewritten
    network back to CIRCT IR.
  }];
}

def MockturtleFunctionalReduction
    : MockturtlePassBase<"synth-mockturtle-functional-reduction",
                         "hw::HWModuleOp"> {
  let summary = "Run mockturtle functional reduction";
}

def MockturtleMIGAlgebraicRewriteDepth
    : MockturtlePassBase<"synth-mockturtle-mig-algebraic-rewrite-depth",
                         "hw::HWModuleOp"> {
  let summary = "Run mockturtle MIG algebraic depth rewriting";
}

def MockturtleSOPBalancing
    : MockturtlePassBase<"synth-mockturtle-sop-balancing", "hw::HWModuleOp"> {
  let summary = "Run mockturtle SOP balancing";
}

def MockturtleEmap
    : MockturtlePassBase<"synth-mockturtle-emap", "mlir::ModuleOp"> {
  let summary = "Run mockturtle extended technology mapping";
  let options = [
    Option<"genlibPath", "genlib-path", "std::string", "",
           "Path to the Genlib file describing the target library">
  ];
}

#endif // CIRCT_MOCKTURTLE_PASSES
