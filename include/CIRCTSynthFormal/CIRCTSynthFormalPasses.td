//===-- Passes.td - FRAIG LEC pass definitions ------------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef CIRCT_SYNTHFORMAL_PASSES_TD
#define CIRCT_SYNTHFORMAL_PASSES_TD

include "mlir/Pass/PassBase.td"
include "mlir/Rewrite/PassUtil.td"

def LowerLECToMiter : Pass<"fraig-lec-lower-to-miter", "::mlir::ModuleOp"> {
  let summary = "Lower verif.lec operations to hw.module miters";
  let description = [{
    Create one miter module per resultless verif.lec operation. The pass
    predeclares current-state cut-point inputs for all matched named registers,
    but only clones the top-level LEC regions. Instance expansion and register
    matching are intentionally handled by later passes so cleanup can run
    between hierarchy layers.
  }];
  let dependentDialects = [
    "circt::comb::CombDialect",
    "circt::hw::HWDialect",
    "circt::seq::SeqDialect",
    "circt::synth::SynthDialect",
    "circt::verif::VerifDialect"
  ];
}

def LowerBMCToMiter : Pass<"fraig-lec-lower-bmc-to-miter", "::mlir::ModuleOp"> {
  let summary = "Lower verif.bmc operations to unrolled hw.module miters";
  let description = [{
    Unroll each verif.bmc operation into a combinational miter module. The pass
    clones the init region once, clones the circuit and loop regions for each
    bounded step, turns fresh symbolic circuit inputs into miter inputs, and
    emits one fail bit guarded by the in-scope assumptions.
  }];
  let dependentDialects = [
    "circt::comb::CombDialect",
    "circt::hw::HWDialect",
    "circt::seq::SeqDialect",
    "circt::verif::VerifDialect"
  ];
}

def MatchRegisters : Pass<"fraig-lec-match-registers", "::mlir::ModuleOp"> {
  let summary = "Match exposed miter registers and replace them with choices";
  let description = [{
    Match currently exposed left/right registers by hierarchical name, replace
    their current-state values with a synth.choice over predeclared state
    inputs, and OR the next-state mismatch into the miter fail output.
  }];
  let dependentDialects = [
    "circt::comb::CombDialect",
    "circt::hw::HWDialect",
    "circt::seq::SeqDialect",
    "circt::synth::SynthDialect"
  ];
}

def InlineInstanceLayer : Pass<"fraig-lec-inline-instance-layer",
                               "::mlir::ModuleOp"> {
  let summary = "Inline one layer of miter-local hw.instance operations";
  let description = [{
    Inline currently exposed miter-local hw.instance operations by one
    hierarchy level, preserving side and hierarchical path metadata on newly
    exposed instances and registers. The pass deliberately does not recurse;
    use it repeatedly with register matching and cleanup between iterations.
  }];
  let dependentDialects = [
    "circt::comb::CombDialect",
    "circt::hw::HWDialect",
    "circt::seq::SeqDialect"
  ];
}

def PruneNonMiterModules : Pass<"fraig-lec-prune-non-miters",
                                "::mlir::ModuleOp"> {
  let summary = "Remove helper modules after miter-local inlining";
  let description = [{
    Drop non-miter hw.module operations after the LEC miter has been fully
    materialized.
  }];
  let dependentDialects = ["circt::hw::HWDialect"];
}

def ResolveChoices : Pass<"fraig-lec-resolve-choices", "::mlir::ModuleOp"> {
  let summary = "Resolve synth.choice operations";
  let description = [{
    Resolve synth.choice operations to their first input after functional
    reduction has proven the choice equivalent.
  }];
  let dependentDialects = ["circt::synth::SynthDialect"];
}

#endif // CIRCT_SYNTHFORMAL_PASSES_TD
