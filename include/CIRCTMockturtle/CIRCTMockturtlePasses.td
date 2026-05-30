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
  let options = [
    Option<"maxPIs", "max-pis", "unsigned", "6",
           "Maximum number of PIs in each MFFC or reconvergence cut">,
    Option<"allowZeroGain", "allow-zero-gain", "bool", "false",
           "Allow zero-gain substitutions">,
    Option<"useReconvergenceCut", "use-reconvergence-cut", "bool", "true",
           "Use reconvergence-driven cuts for large MFFCs">,
    Option<"useDontCares", "use-dont-cares", "bool", "false",
           "Use don't cares for optimization">,
    Option<"progress", "progress", "bool", "false", "Show progress">,
    Option<"verbose", "verbose", "bool", "false", "Print mockturtle stats">
  ];
}

def MockturtleFunctionalReduction
    : MockturtlePassBase<"synth-mockturtle-functional-reduction",
                         "hw::HWModuleOp"> {
  let summary = "Run mockturtle functional reduction";
  let options = [
    Option<"progress", "progress", "bool", "false", "Show progress">,
    Option<"verbose", "verbose", "bool", "false", "Print mockturtle stats">,
    Option<"maxIterations", "max-iterations", "unsigned", "10",
           "Maximum reduction iterations, or 0 to repeat to fixpoint">,
    Option<"patternFilename", "pattern-filename", "std::string", "",
           "Load simulation patterns from this file">,
    Option<"savePatterns", "save-patterns", "std::string", "",
           "Save appended simulation patterns to this file">,
    Option<"maxTFINodes", "max-tfi-nodes", "unsigned", "1000",
           "Maximum nodes in the transitive fanin cone to compare">,
    Option<"skipFanoutLimit", "skip-fanout-limit", "unsigned", "100",
           "Maximum explored fanout count for TFI nodes">,
    Option<"conflictLimit", "conflict-limit", "unsigned", "100",
           "SAT solver conflict limit">,
    Option<"maxClauses", "max-clauses", "unsigned", "1000",
           "Maximum incremental SAT clauses">,
    Option<"numPatterns", "num-patterns", "unsigned", "256",
           "Initial number of random simulation patterns">,
    Option<"maxPatterns", "max-patterns", "unsigned", "1024",
           "Maximum simulation patterns before reseeding">
  ];
}

def MockturtleMIGAlgebraicRewriteDepth
    : MockturtlePassBase<"synth-mockturtle-mig-algebraic-rewrite-depth",
                         "hw::HWModuleOp"> {
  let summary = "Run mockturtle MIG algebraic depth rewriting";
  let options = [
    Option<"strategy", "strategy", "std::string", "\"dfs\"",
           "Rewrite strategy: dfs, aggressive, or selective">,
    Option<"overhead", "overhead", "double", "2.0",
           "Overhead factor used by aggressive rewriting">,
    Option<"allowAreaIncrease", "allow-area-increase", "bool", "true",
           "Allow area increase while optimizing depth">
  ];
}

def MockturtleXAGAlgebraicRewriteDepth
    : MockturtlePassBase<"synth-mockturtle-xag-algebraic-rewrite-depth",
                         "hw::HWModuleOp"> {
  let summary = "Run mockturtle XAG algebraic depth rewriting";
  let options = [
    Option<"strategy", "strategy", "std::string", "\"dfs\"",
           "Rewrite strategy: dfs, aggressive, or selective">,
    Option<"overhead", "overhead", "double", "2.0",
           "Overhead factor used by aggressive rewriting">,
    Option<"allowAreaIncrease", "allow-area-increase", "bool", "true",
           "Allow area increase while optimizing depth">
  ];
}

def MockturtleXMGAlgebraicRewriteDepth
    : MockturtlePassBase<"synth-mockturtle-xmg-algebraic-rewrite-depth",
                         "hw::HWModuleOp"> {
  let summary = "Run mockturtle XMG algebraic depth rewriting";
  let options = [
    Option<"strategy", "strategy", "std::string", "\"dfs\"",
           "Rewrite strategy: dfs, aggressive, or selective">,
    Option<"overhead", "overhead", "double", "2.0",
           "Overhead factor used by aggressive rewriting">,
    Option<"allowAreaIncrease", "allow-area-increase", "bool", "true",
           "Allow area increase while optimizing depth">
  ];
}

class MockturtleLUTBalancingBase<string name>
    : MockturtlePassBase<name, "hw::HWModuleOp"> {
  let options = [
    Option<"cutSize", "cut-size", "unsigned", "6",
           "Cut size for LUT mapping">,
    Option<"cutLimit", "cut-limit", "unsigned", "8",
           "Cut limit for LUT mapping">,
    Option<"areaOrientedMapping", "area-oriented-mapping", "bool", "false",
           "Do area-oriented mapping">,
    Option<"requiredDelay", "required-delay", "unsigned", "0",
           "Required depth for depth relaxation">,
    Option<"relaxRequired", "relax-required", "unsigned", "0",
           "Required depth relaxation ratio in percent">,
    Option<"recomputeCuts", "recompute-cuts", "bool", "true",
           "Recompute cuts at each step">,
    Option<"areaShareRounds", "area-share-rounds", "unsigned", "2",
           "Number of area sharing optimization rounds">,
    Option<"areaFlowRounds", "area-flow-rounds", "unsigned", "1",
           "Number of area flow optimization rounds">,
    Option<"elaRounds", "ela-rounds", "unsigned", "2",
           "Number of exact area optimization rounds">,
    Option<"edgeOptimization", "edge-optimization", "bool", "true",
           "Use edge-count reduction">,
    Option<"cutExpansion", "cut-expansion", "bool", "true",
           "Try to expand cuts">,
    Option<"removeDominatedCuts", "remove-dominated-cuts", "bool", "true",
           "Remove cuts contained in other cuts">,
    Option<"costCacheVars", "cost-cache-vars", "unsigned", "3",
           "Maximum variables for cost-function caching">,
    Option<"verbose", "verbose", "bool", "false", "Print mockturtle stats">
  ];
}

def MockturtleSOPBalancing
    : MockturtleLUTBalancingBase<"synth-mockturtle-sop-balancing"> {
  let summary = "Run mockturtle SOP balancing";
}

def MockturtleESOPBalancing
    : MockturtleLUTBalancingBase<"synth-mockturtle-esop-balancing"> {
  let summary = "Run mockturtle ESOP balancing";
}

def MockturtleAIGBalancing
    : MockturtlePassBase<"synth-mockturtle-aig-balancing", "hw::HWModuleOp"> {
  let summary = "Run mockturtle AIG tree balancing";
  let options = [
    Option<"minimizeLevels", "minimize-levels", "bool", "true",
           "Minimize AIG levels">,
    Option<"fastMode", "fast-mode", "bool", "true",
           "Use faster balancing mode">
  ];
}

def MockturtleXAGBalancing
    : MockturtlePassBase<"synth-mockturtle-xag-balancing", "hw::HWModuleOp"> {
  let summary = "Run mockturtle XAG tree balancing";
  let options = [
    Option<"minimizeLevels", "minimize-levels", "bool", "true",
           "Minimize XAG levels">,
    Option<"fastMode", "fast-mode", "bool", "true",
           "Use faster balancing mode">
  ];
}

class MockturtleResubstitutionBase<string name>
    : MockturtlePassBase<name, "hw::HWModuleOp"> {
  let options = [
    Option<"maxPIs", "max-pis", "unsigned", "8",
           "Maximum number of PIs in reconvergence-driven cuts">,
    Option<"maxDivisors", "max-divisors", "unsigned", "150",
           "Maximum divisors to consider">,
    Option<"maxInserts", "max-inserts", "unsigned", "2",
           "Maximum nodes added by resubstitution">,
    Option<"skipFanoutLimitForRoots", "skip-fanout-limit-for-roots",
           "unsigned", "1000",
           "Maximum fanout of a node considered as a root">,
    Option<"skipFanoutLimitForDivisors", "skip-fanout-limit-for-divisors",
           "unsigned", "100",
           "Maximum fanout of a node considered as a divisor">,
    Option<"progress", "progress", "bool", "false", "Show progress">,
    Option<"verbose", "verbose", "bool", "false", "Print mockturtle stats">,
    Option<"useDontCares", "use-dont-cares", "bool", "false",
           "Use don't cares for window-based resubstitution">,
    Option<"windowSize", "window-size", "unsigned", "12",
           "Window size for don't-care calculation">,
    Option<"preserveDepth", "preserve-depth", "bool", "false",
           "Prevent depth increase when supported by the engine">,
    Option<"patternFilename", "pattern-filename", "std::string", "",
           "Load simulation patterns from this file">,
    Option<"savePatterns", "save-patterns", "std::string", "",
           "Save appended simulation patterns to this file">,
    Option<"maxClauses", "max-clauses", "unsigned", "1000",
           "Maximum SAT clauses for simulation-based resubstitution">,
    Option<"conflictLimit", "conflict-limit", "unsigned", "1000",
           "SAT solver conflict limit for simulation-based resubstitution">,
    Option<"randomSeed", "random-seed", "unsigned", "1",
           "SAT random seed">,
    Option<"odcLevels", "odc-levels", "int", "0",
           "ODC levels: 0 disables, -1 considers TFO to PO">,
    Option<"maxTrials", "max-trials", "unsigned", "100",
           "Maximum trials for the resubstitution functor">,
    Option<"maxDivisorsK", "max-divisors-k", "unsigned", "50",
           "Maximum divisors for k-resubstitution">
  ];
}

def MockturtleAIGResubstitution
    : MockturtleResubstitutionBase<"synth-mockturtle-aig-resubstitution"> {
  let summary = "Run mockturtle AIG-specific resubstitution";
}

def MockturtleAIGResubstitution2
    : MockturtleResubstitutionBase<"synth-mockturtle-aig-resubstitution2"> {
  let summary = "Run mockturtle window-based AIG-specific resubstitution";
}

def MockturtleXAGResubstitution
    : MockturtleResubstitutionBase<"synth-mockturtle-xag-resubstitution"> {
  let summary = "Run mockturtle XAG-specific resubstitution";
}

def MockturtleMIGResubstitution
    : MockturtleResubstitutionBase<"synth-mockturtle-mig-resubstitution"> {
  let summary = "Run mockturtle MIG-specific resubstitution";
}

def MockturtleMIGResubstitution2
    : MockturtleResubstitutionBase<"synth-mockturtle-mig-resubstitution2"> {
  let summary = "Run mockturtle window-based MIG-specific resubstitution";
}

def MockturtleXMGResubstitution
    : MockturtleResubstitutionBase<"synth-mockturtle-xmg-resubstitution"> {
  let summary = "Run mockturtle XMG-specific resubstitution";
}

def MockturtleMIGInverterPropagation
    : MockturtlePassBase<"synth-mockturtle-mig-inv-propagation",
                         "hw::HWModuleOp"> {
  let summary = "Run mockturtle MIG inverter propagation";
}

def MockturtleMIGInverterOptimization
    : MockturtlePassBase<"synth-mockturtle-mig-inv-optimization",
                         "hw::HWModuleOp"> {
  let summary = "Run mockturtle MIG inverter optimization";
}

def MockturtleEmap
    : MockturtlePassBase<"synth-mockturtle-emap", "mlir::ModuleOp"> {
  let summary = "Run mockturtle extended technology mapping";
  let options = [
    Option<"genlibPath", "genlib-path", "std::string", "",
           "Path to the Genlib file describing the target library">,
    Option<"cutLimit", "cut-limit", "unsigned", "16",
           "Cut limit for emap cut enumeration">,
    Option<"areaOrientedMapping", "area-oriented-mapping", "bool", "false",
           "Do area-oriented mapping">,
    Option<"mapMultioutput", "map-multioutput", "bool", "true",
           "Map using multi-output gates">,
    Option<"matchingMode", "matching-mode", "std::string", "\"hybrid\"",
           "Matching mode: boolean, structural, or hybrid">,
    Option<"requiredTime", "required-time", "double", "0.0",
           "Target required time for each PO">,
    Option<"relaxRequired", "relax-required", "double", "0.0",
           "Required time relaxation in percent">,
    Option<"areaFlowRounds", "area-flow-rounds", "unsigned", "3",
           "Number of area flow optimization rounds">,
    Option<"elaRounds", "ela-rounds", "unsigned", "2",
           "Number of exact area optimization rounds">,
    Option<"eswpRounds", "eswp-rounds", "unsigned", "0",
           "Number of exact switching power optimization rounds">,
    Option<"switchingActivityPatterns", "switching-activity-patterns",
           "unsigned", "2048",
           "Number of patterns for switching activity computation">,
    Option<"useMatchAlternatives", "use-match-alternatives", "bool", "true",
           "Compute area-oriented alternative matches">,
    Option<"removeDominatedCuts", "remove-dominated-cuts", "bool", "false",
           "Remove cuts contained in other cuts">,
    Option<"removeOverlappingMulticuts", "remove-overlapping-multicuts",
           "bool", "false", "Remove overlapping multi-output cuts">,
    Option<"verbose", "verbose", "bool", "false", "Print mockturtle stats">
  ];
}

#endif // CIRCT_MOCKTURTLE_PASSES
