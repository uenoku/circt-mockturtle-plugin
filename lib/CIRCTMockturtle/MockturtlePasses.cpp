//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements trivial mockturtle passes that are simple bindings
// to mockturtle transformation algorithms.
//
//===----------------------------------------------------------------------===//

#include "CIRCTMockturtle/CIRCTMockturtlePasses.h"
#include "NetworkConversion.h"
#include "circt/Dialect/Comb/CombDialect.h"
#include "circt/Dialect/HW/HWOps.h"
#include "circt/Dialect/Synth/SynthDialect.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "synth-mockturtle-passes"

namespace circt {
namespace mockturtle_plugin {
#define GEN_PASS_DEF_MOCKTURTLEREFACTOR
#define GEN_PASS_DEF_MOCKTURTLEFUNCTIONALREDUCTION
#define GEN_PASS_DEF_MOCKTURTLEMIGALGEBRAICREWRITEDEPTH
#define GEN_PASS_DEF_MOCKTURTLEXAGALGEBRAICREWRITEDEPTH
#define GEN_PASS_DEF_MOCKTURTLEXMGALGEBRAICREWRITEDEPTH
#define GEN_PASS_DEF_MOCKTURTLESOPBALANCING
#define GEN_PASS_DEF_MOCKTURTLEESOPBALANCING
#define GEN_PASS_DEF_MOCKTURTLEAIGBALANCING
#define GEN_PASS_DEF_MOCKTURTLEXAGBALANCING
#define GEN_PASS_DEF_MOCKTURTLEAIGRESUBSTITUTION
#define GEN_PASS_DEF_MOCKTURTLEAIGRESUBSTITUTION2
#define GEN_PASS_DEF_MOCKTURTLEAIGTOXAG
#define GEN_PASS_DEF_MOCKTURTLEAIGTOMIG
#define GEN_PASS_DEF_MOCKTURTLEAIGTOXMG
#define GEN_PASS_DEF_MOCKTURTLEXAGRESUBSTITUTION
#define GEN_PASS_DEF_MOCKTURTLEMIGRESUBSTITUTION
#define GEN_PASS_DEF_MOCKTURTLEMIGRESUBSTITUTION2
#define GEN_PASS_DEF_MOCKTURTLEXMGRESUBSTITUTION
#define GEN_PASS_DEF_MOCKTURTLEMIGINVERTERPROPAGATION
#define GEN_PASS_DEF_MOCKTURTLEMIGINVERTEROPTIMIZATION
#include "CIRCTMockturtle/CIRCTMockturtlePasses.h.inc"
} // namespace mockturtle_plugin
} // namespace circt

using namespace circt;
using namespace circt::mockturtle_plugin;
using namespace circt::mockturtle_plugin::mockturtle_integration;

namespace {

ResubstitutionOptions getResubstitutionOptions(
    unsigned maxPIs, unsigned maxDivisors, unsigned maxInserts,
    unsigned skipRoots, unsigned skipDivisors, bool progress, bool verbose,
    bool useDontCares, unsigned windowSize, bool preserveDepth,
    StringRef patternFilename, StringRef savePatterns, unsigned maxClauses,
    unsigned conflictLimit, unsigned randomSeed, int odcLevels,
    unsigned maxTrials, unsigned maxDivisorsK) {
  ResubstitutionOptions options;
  options.maxPIs = maxPIs;
  options.maxDivisors = maxDivisors;
  options.maxInserts = maxInserts;
  options.skipFanoutLimitForRoots = skipRoots;
  options.skipFanoutLimitForDivisors = skipDivisors;
  options.progress = progress;
  options.verbose = verbose;
  options.useDontCares = useDontCares;
  options.windowSize = windowSize;
  options.preserveDepth = preserveDepth;
  options.patternFilename = patternFilename.str();
  options.savePatterns = savePatterns.str();
  options.maxClauses = maxClauses;
  options.conflictLimit = conflictLimit;
  options.randomSeed = randomSeed;
  options.odcLevels = odcLevels;
  options.maxTrials = maxTrials;
  options.maxDivisorsK = maxDivisorsK;
  return options;
}

//===----------------------------------------------------------------------===//
// Refactor Pass
//===----------------------------------------------------------------------===//

/// Refactor pass using mockturtle algorithms for logic optimization.
/// This pass performs logic refactoring using reconvergence-driven cuts
/// to optimize circuit structure.
struct MockturtleRefactorPass
    : public impl::MockturtleRefactorBase<MockturtleRefactorPass> {
  using impl::MockturtleRefactorBase<
      MockturtleRefactorPass>::MockturtleRefactorBase;

  void runOnOperation() override {
    auto module = getOperation();
    LLVM_DEBUG(llvm::dbgs() << "Running Mockturtle Refactor pass on module: "
                            << module.getModuleName() << "\n");
    RefactoringOptions options;
    options.maxPIs = maxPIs;
    options.allowZeroGain = allowZeroGain;
    options.useReconvergenceCut = useReconvergenceCut;
    options.useDontCares = useDontCares;
    options.progress = progress;
    options.verbose = verbose;
    if (failed(runNetworkTransforms(module.getBodyBlock(), [&](Ntk &ntk) {
          return runSOPRefactoring(ntk, options);
        })))
      signalPassFailure();
  }
};

//===----------------------------------------------------------------------===//
// Functional Reduction Pass
//===----------------------------------------------------------------------===//

/// Functional reduction pass using mockturtle's SAT-based optimization.
/// This pass identifies and removes functionally redundant nodes using
/// SAT-based equivalence checking with simulation-based filtering.
struct MockturtleFunctionalReductionPass
    : public impl::MockturtleFunctionalReductionBase<
          MockturtleFunctionalReductionPass> {
  using impl::MockturtleFunctionalReductionBase<
      MockturtleFunctionalReductionPass>::MockturtleFunctionalReductionBase;

  void runOnOperation() override {
    auto module = getOperation();
    LLVM_DEBUG(llvm::dbgs()
               << "Running Mockturtle Functional Reduction pass on module: "
               << module.getModuleName() << "\n");
    FunctionalReductionOptions options;
    options.progress = progress;
    options.verbose = verbose;
    options.maxIterations = maxIterations;
    options.patternFilename = patternFilename;
    options.savePatterns = savePatterns;
    options.maxTFINodes = maxTFINodes;
    options.skipFanoutLimit = skipFanoutLimit;
    options.conflictLimit = conflictLimit;
    options.maxClauses = maxClauses;
    options.numPatterns = numPatterns;
    options.maxPatterns = maxPatterns;
    if (failed(runNetworkTransforms(module.getBodyBlock(), [&](Ntk &ntk) {
          return runFunctionalReduction(ntk, options);
        })))
      signalPassFailure();
  }
};

//===----------------------------------------------------------------------===//
// MIG Algebraic Rewrite Depth Pass
//===----------------------------------------------------------------------===//

/// MIG algebraic depth rewriting pass for depth optimization.
/// This pass applies MIG algebraic rewriting rules to reduce circuit depth
/// using DFS strategy.
struct MockturtleMIGAlgebraicRewriteDepthPass
    : public impl::MockturtleMIGAlgebraicRewriteDepthBase<
          MockturtleMIGAlgebraicRewriteDepthPass> {
  using impl::MockturtleMIGAlgebraicRewriteDepthBase<
      MockturtleMIGAlgebraicRewriteDepthPass>::
      MockturtleMIGAlgebraicRewriteDepthBase;

  void runOnOperation() override {
    auto module = getOperation();
    LLVM_DEBUG(llvm::dbgs()
               << "Running Mockturtle MIG Algebraic Rewrite Depth pass on "
                  "module: "
               << module.getModuleName() << "\n");
    MIGAlgebraicRewriteDepthOptions options;
    options.strategy = strategy;
    options.overhead = static_cast<float>(overhead);
    options.allowAreaIncrease = allowAreaIncrease;
    if (failed(runMIGNetworkTransforms(
            module.getBodyBlock(), [&](mockturtle::mig_network &ntk) {
              return runMIGAlgebraicRewriteDepth(ntk, options);
            })))
      signalPassFailure();
  }
};

//===----------------------------------------------------------------------===//
// XAG Algebraic Rewrite Depth Pass
//===----------------------------------------------------------------------===//
struct MockturtleXAGAlgebraicRewriteDepthPass
    : public impl::MockturtleXAGAlgebraicRewriteDepthBase<
          MockturtleXAGAlgebraicRewriteDepthPass> {
  using impl::MockturtleXAGAlgebraicRewriteDepthBase<
      MockturtleXAGAlgebraicRewriteDepthPass>::
      MockturtleXAGAlgebraicRewriteDepthBase;

  void runOnOperation() override {
    auto module = getOperation();
    LLVM_DEBUG(llvm::dbgs()
               << "Running Mockturtle XAG Algebraic Rewrite Depth pass on "
                  "module: "
               << module.getModuleName() << "\n");
    MIGAlgebraicRewriteDepthOptions options;
    options.strategy = strategy;
    options.overhead = static_cast<float>(overhead);
    options.allowAreaIncrease = allowAreaIncrease;
    if (failed(runXAGNetworkTransforms(
            module.getBodyBlock(), [&](mockturtle::xag_network &ntk) {
              return runXAGAlgebraicRewriteDepth(ntk, options);
            })))
      signalPassFailure();
  }
};

//===----------------------------------------------------------------------===//
// XMG Algebraic Rewrite Depth Pass
//===----------------------------------------------------------------------===//
struct MockturtleXMGAlgebraicRewriteDepthPass
    : public impl::MockturtleXMGAlgebraicRewriteDepthBase<
          MockturtleXMGAlgebraicRewriteDepthPass> {
  using impl::MockturtleXMGAlgebraicRewriteDepthBase<
      MockturtleXMGAlgebraicRewriteDepthPass>::
      MockturtleXMGAlgebraicRewriteDepthBase;

  void runOnOperation() override {
    auto module = getOperation();
    LLVM_DEBUG(llvm::dbgs()
               << "Running Mockturtle XMG Algebraic Rewrite Depth pass on "
                  "module: "
               << module.getModuleName() << "\n");
    MIGAlgebraicRewriteDepthOptions options;
    options.strategy = strategy;
    options.overhead = static_cast<float>(overhead);
    options.allowAreaIncrease = allowAreaIncrease;
    if (failed(runXMGNetworkTransforms(
            module.getBodyBlock(), [&](mockturtle::xmg_network &ntk) {
              return runXMGAlgebraicRewriteDepth(ntk, options);
            })))
      signalPassFailure();
  }
};

//===----------------------------------------------------------------------===//
// SOP Balancing Pass
//===----------------------------------------------------------------------===//
/// SOP balancing pass using mockturtle's LUT-based balancing algorithm.
/// This pass restructures the circuit using SOP balancing to optimize
/// depth and area.
struct MockturtleSOPBalancingPass
    : public impl::MockturtleSOPBalancingBase<MockturtleSOPBalancingPass> {
  using impl::MockturtleSOPBalancingBase<
      MockturtleSOPBalancingPass>::MockturtleSOPBalancingBase;

  void runOnOperation() override {
    auto module = getOperation();
    LLVM_DEBUG(llvm::dbgs()
               << "Running Mockturtle SOP Balancing pass on module: "
               << module.getModuleName() << "\n");
    SOPBalancingOptions options;
    options.cutSize = cutSize;
    options.cutLimit = cutLimit;
    options.areaOrientedMapping = areaOrientedMapping;
    options.requiredDelay = requiredDelay;
    options.relaxRequired = relaxRequired;
    options.recomputeCuts = recomputeCuts;
    options.areaShareRounds = areaShareRounds;
    options.areaFlowRounds = areaFlowRounds;
    options.elaRounds = elaRounds;
    options.edgeOptimization = edgeOptimization;
    options.cutExpansion = cutExpansion;
    options.removeDominatedCuts = removeDominatedCuts;
    options.costCacheVars = costCacheVars;
    options.verbose = verbose;
    if (failed(runNetworkTransforms(module.getBodyBlock(), [&](Ntk &ntk) {
          return runSOPBalancing(ntk, options);
        })))
      signalPassFailure();
  }
};

//===----------------------------------------------------------------------===//
// ESOP Balancing Pass
//===----------------------------------------------------------------------===//
struct MockturtleESOPBalancingPass
    : public impl::MockturtleESOPBalancingBase<MockturtleESOPBalancingPass> {
  using impl::MockturtleESOPBalancingBase<
      MockturtleESOPBalancingPass>::MockturtleESOPBalancingBase;

  void runOnOperation() override {
    auto module = getOperation();
    LLVM_DEBUG(llvm::dbgs()
               << "Running Mockturtle ESOP Balancing pass on module: "
               << module.getModuleName() << "\n");
    SOPBalancingOptions options;
    options.cutSize = cutSize;
    options.cutLimit = cutLimit;
    options.areaOrientedMapping = areaOrientedMapping;
    options.requiredDelay = requiredDelay;
    options.relaxRequired = relaxRequired;
    options.recomputeCuts = recomputeCuts;
    options.areaShareRounds = areaShareRounds;
    options.areaFlowRounds = areaFlowRounds;
    options.elaRounds = elaRounds;
    options.edgeOptimization = edgeOptimization;
    options.cutExpansion = cutExpansion;
    options.removeDominatedCuts = removeDominatedCuts;
    options.costCacheVars = costCacheVars;
    options.verbose = verbose;
    if (failed(runNetworkTransforms(module.getBodyBlock(), [&](Ntk &ntk) {
          return runESOPBalancing(ntk, options);
        })))
      signalPassFailure();
  }
};

//===----------------------------------------------------------------------===//
// AIG Balancing Pass
//===----------------------------------------------------------------------===//
struct MockturtleAIGBalancingPass
    : public impl::MockturtleAIGBalancingBase<MockturtleAIGBalancingPass> {
  using impl::MockturtleAIGBalancingBase<
      MockturtleAIGBalancingPass>::MockturtleAIGBalancingBase;

  void runOnOperation() override {
    auto module = getOperation();
    LLVM_DEBUG(llvm::dbgs()
               << "Running Mockturtle AIG Balancing pass on module: "
               << module.getModuleName() << "\n");
    AIGBalancingOptions options;
    options.minimizeLevels = minimizeLevels;
    options.fastMode = fastMode;
    if (failed(runAIGNetworkTransforms(module.getBodyBlock(),
                                       [&](mockturtle::aig_network &ntk) {
                                         return runAIGBalancing(ntk, options);
                                       })))
      signalPassFailure();
  }
};

//===----------------------------------------------------------------------===//
// XAG Balancing Pass
//===----------------------------------------------------------------------===//
struct MockturtleXAGBalancingPass
    : public impl::MockturtleXAGBalancingBase<MockturtleXAGBalancingPass> {
  using impl::MockturtleXAGBalancingBase<
      MockturtleXAGBalancingPass>::MockturtleXAGBalancingBase;

  void runOnOperation() override {
    auto module = getOperation();
    LLVM_DEBUG(llvm::dbgs()
               << "Running Mockturtle XAG Balancing pass on module: "
               << module.getModuleName() << "\n");
    AIGBalancingOptions options;
    options.minimizeLevels = minimizeLevels;
    options.fastMode = fastMode;
    if (failed(runXAGNetworkTransforms(module.getBodyBlock(),
                                       [&](mockturtle::xag_network &ntk) {
                                         return runXAGBalancing(ntk, options);
                                       })))
      signalPassFailure();
  }
};

//===----------------------------------------------------------------------===//
// AIG to XAG Conversion Pass
//===----------------------------------------------------------------------===//
struct MockturtleAIGToXAGPass
    : public impl::MockturtleAIGToXAGBase<MockturtleAIGToXAGPass> {
  using impl::MockturtleAIGToXAGBase<
      MockturtleAIGToXAGPass>::MockturtleAIGToXAGBase;

  void runOnOperation() override {
    auto module = getOperation();
    LLVM_DEBUG(llvm::dbgs() << "Running Mockturtle AIG to XAG pass on module: "
                            << module.getModuleName() << "\n");
    AIGToGraphConversionOptions options;
    options.verbose = verbose;
    if (failed(runAIGToXAGNetworkConversion(module.getBodyBlock(), options)))
      signalPassFailure();
  }
};

//===----------------------------------------------------------------------===//
// AIG to MIG Conversion Pass
//===----------------------------------------------------------------------===//
struct MockturtleAIGToMIGPass
    : public impl::MockturtleAIGToMIGBase<MockturtleAIGToMIGPass> {
  using impl::MockturtleAIGToMIGBase<
      MockturtleAIGToMIGPass>::MockturtleAIGToMIGBase;

  void runOnOperation() override {
    auto module = getOperation();
    LLVM_DEBUG(llvm::dbgs() << "Running Mockturtle AIG to MIG pass on module: "
                            << module.getModuleName() << "\n");
    AIGToGraphConversionOptions options;
    options.useMultiple = useMultiple;
    options.verbose = verbose;
    if (failed(runAIGToMIGNetworkConversion(module.getBodyBlock(), options)))
      signalPassFailure();
  }
};

//===----------------------------------------------------------------------===//
// AIG to XMG Conversion Pass
//===----------------------------------------------------------------------===//
struct MockturtleAIGToXMGPass
    : public impl::MockturtleAIGToXMGBase<MockturtleAIGToXMGPass> {
  using impl::MockturtleAIGToXMGBase<
      MockturtleAIGToXMGPass>::MockturtleAIGToXMGBase;

  void runOnOperation() override {
    auto module = getOperation();
    LLVM_DEBUG(llvm::dbgs() << "Running Mockturtle AIG to XMG pass on module: "
                            << module.getModuleName() << "\n");
    AIGToGraphConversionOptions options;
    options.verbose = verbose;
    if (failed(runAIGToXMGNetworkConversion(module.getBodyBlock(), options)))
      signalPassFailure();
  }
};

//===----------------------------------------------------------------------===//
// AIG Resubstitution Pass
//===----------------------------------------------------------------------===//
struct MockturtleAIGResubstitutionPass
    : public impl::MockturtleAIGResubstitutionBase<
          MockturtleAIGResubstitutionPass> {
  using impl::MockturtleAIGResubstitutionBase<
      MockturtleAIGResubstitutionPass>::MockturtleAIGResubstitutionBase;

  void runOnOperation() override {
    auto module = getOperation();
    LLVM_DEBUG(llvm::dbgs()
               << "Running Mockturtle AIG Resubstitution pass on module: "
               << module.getModuleName() << "\n");
    auto options = getResubstitutionOptions(
        maxPIs, maxDivisors, maxInserts, skipFanoutLimitForRoots,
        skipFanoutLimitForDivisors, progress, verbose, useDontCares, windowSize,
        preserveDepth, patternFilename, savePatterns, maxClauses, conflictLimit,
        randomSeed, odcLevels, maxTrials, maxDivisorsK);
    if (failed(runAIGNetworkTransforms(
            module.getBodyBlock(), [&](mockturtle::aig_network &ntk) {
              return runAIGResubstitution(ntk, options);
            })))
      signalPassFailure();
  }
};

//===----------------------------------------------------------------------===//
// AIG Resubstitution2 Pass
//===----------------------------------------------------------------------===//
struct MockturtleAIGResubstitution2Pass
    : public impl::MockturtleAIGResubstitution2Base<
          MockturtleAIGResubstitution2Pass> {
  using impl::MockturtleAIGResubstitution2Base<
      MockturtleAIGResubstitution2Pass>::MockturtleAIGResubstitution2Base;

  void runOnOperation() override {
    auto module = getOperation();
    LLVM_DEBUG(llvm::dbgs()
               << "Running Mockturtle AIG Resubstitution2 pass on module: "
               << module.getModuleName() << "\n");
    auto options = getResubstitutionOptions(
        maxPIs, maxDivisors, maxInserts, skipFanoutLimitForRoots,
        skipFanoutLimitForDivisors, progress, verbose, useDontCares, windowSize,
        preserveDepth, patternFilename, savePatterns, maxClauses, conflictLimit,
        randomSeed, odcLevels, maxTrials, maxDivisorsK);
    if (failed(runAIGNetworkTransforms(
            module.getBodyBlock(), [&](mockturtle::aig_network &ntk) {
              return runAIGResubstitution2(ntk, options);
            })))
      signalPassFailure();
  }
};

//===----------------------------------------------------------------------===//
// XAG Resubstitution Pass
//===----------------------------------------------------------------------===//
struct MockturtleXAGResubstitutionPass
    : public impl::MockturtleXAGResubstitutionBase<
          MockturtleXAGResubstitutionPass> {
  using impl::MockturtleXAGResubstitutionBase<
      MockturtleXAGResubstitutionPass>::MockturtleXAGResubstitutionBase;

  void runOnOperation() override {
    auto module = getOperation();
    LLVM_DEBUG(llvm::dbgs()
               << "Running Mockturtle XAG Resubstitution pass on module: "
               << module.getModuleName() << "\n");
    auto options = getResubstitutionOptions(
        maxPIs, maxDivisors, maxInserts, skipFanoutLimitForRoots,
        skipFanoutLimitForDivisors, progress, verbose, useDontCares, windowSize,
        preserveDepth, patternFilename, savePatterns, maxClauses, conflictLimit,
        randomSeed, odcLevels, maxTrials, maxDivisorsK);
    if (failed(runXAGNetworkTransforms(
            module.getBodyBlock(), [&](mockturtle::xag_network &ntk) {
              return runXAGResubstitution(ntk, options);
            })))
      signalPassFailure();
  }
};

//===----------------------------------------------------------------------===//
// MIG Resubstitution Pass
//===----------------------------------------------------------------------===//
struct MockturtleMIGResubstitutionPass
    : public impl::MockturtleMIGResubstitutionBase<
          MockturtleMIGResubstitutionPass> {
  using impl::MockturtleMIGResubstitutionBase<
      MockturtleMIGResubstitutionPass>::MockturtleMIGResubstitutionBase;

  void runOnOperation() override {
    auto module = getOperation();
    LLVM_DEBUG(llvm::dbgs()
               << "Running Mockturtle MIG Resubstitution pass on module: "
               << module.getModuleName() << "\n");
    auto options = getResubstitutionOptions(
        maxPIs, maxDivisors, maxInserts, skipFanoutLimitForRoots,
        skipFanoutLimitForDivisors, progress, verbose, useDontCares, windowSize,
        preserveDepth, patternFilename, savePatterns, maxClauses, conflictLimit,
        randomSeed, odcLevels, maxTrials, maxDivisorsK);
    if (failed(runMIGNetworkTransforms(
            module.getBodyBlock(), [&](mockturtle::mig_network &ntk) {
              return runMIGResubstitution(ntk, options);
            })))
      signalPassFailure();
  }
};

//===----------------------------------------------------------------------===//
// MIG Resubstitution2 Pass
//===----------------------------------------------------------------------===//
struct MockturtleMIGResubstitution2Pass
    : public impl::MockturtleMIGResubstitution2Base<
          MockturtleMIGResubstitution2Pass> {
  using impl::MockturtleMIGResubstitution2Base<
      MockturtleMIGResubstitution2Pass>::MockturtleMIGResubstitution2Base;

  void runOnOperation() override {
    auto module = getOperation();
    LLVM_DEBUG(llvm::dbgs()
               << "Running Mockturtle MIG Resubstitution2 pass on module: "
               << module.getModuleName() << "\n");
    auto options = getResubstitutionOptions(
        maxPIs, maxDivisors, maxInserts, skipFanoutLimitForRoots,
        skipFanoutLimitForDivisors, progress, verbose, useDontCares, windowSize,
        preserveDepth, patternFilename, savePatterns, maxClauses, conflictLimit,
        randomSeed, odcLevels, maxTrials, maxDivisorsK);
    if (failed(runMIGNetworkTransforms(
            module.getBodyBlock(), [&](mockturtle::mig_network &ntk) {
              return runMIGResubstitution2(ntk, options);
            })))
      signalPassFailure();
  }
};

//===----------------------------------------------------------------------===//
// XMG Resubstitution Pass
//===----------------------------------------------------------------------===//
struct MockturtleXMGResubstitutionPass
    : public impl::MockturtleXMGResubstitutionBase<
          MockturtleXMGResubstitutionPass> {
  using impl::MockturtleXMGResubstitutionBase<
      MockturtleXMGResubstitutionPass>::MockturtleXMGResubstitutionBase;

  void runOnOperation() override {
    auto module = getOperation();
    LLVM_DEBUG(llvm::dbgs()
               << "Running Mockturtle XMG Resubstitution pass on module: "
               << module.getModuleName() << "\n");
    auto options = getResubstitutionOptions(
        maxPIs, maxDivisors, maxInserts, skipFanoutLimitForRoots,
        skipFanoutLimitForDivisors, progress, verbose, useDontCares, windowSize,
        preserveDepth, patternFilename, savePatterns, maxClauses, conflictLimit,
        randomSeed, odcLevels, maxTrials, maxDivisorsK);
    if (failed(runXMGNetworkTransforms(
            module.getBodyBlock(), [&](mockturtle::xmg_network &ntk) {
              return runXMGResubstitution(ntk, options);
            })))
      signalPassFailure();
  }
};

//===----------------------------------------------------------------------===//
// MIG Inverter Propagation Pass
//===----------------------------------------------------------------------===//
struct MockturtleMIGInverterPropagationPass
    : public impl::MockturtleMIGInverterPropagationBase<
          MockturtleMIGInverterPropagationPass> {
  void runOnOperation() override {
    auto module = getOperation();
    LLVM_DEBUG(llvm::dbgs()
               << "Running Mockturtle MIG Inverter Propagation pass on module: "
               << module.getModuleName() << "\n");
    if (failed(runMIGNetworkTransforms(module.getBodyBlock(),
                                       runMIGInverterPropagation)))
      signalPassFailure();
  }
};

//===----------------------------------------------------------------------===//
// MIG Inverter Optimization Pass
//===----------------------------------------------------------------------===//
struct MockturtleMIGInverterOptimizationPass
    : public impl::MockturtleMIGInverterOptimizationBase<
          MockturtleMIGInverterOptimizationPass> {
  void runOnOperation() override {
    auto module = getOperation();
    LLVM_DEBUG(llvm::dbgs()
               << "Running Mockturtle MIG Inverter Optimization pass on "
                  "module: "
               << module.getModuleName() << "\n");
    if (failed(runMIGNetworkTransforms(module.getBodyBlock(),
                                       runMIGInverterOptimization)))
      signalPassFailure();
  }
};

} // namespace
