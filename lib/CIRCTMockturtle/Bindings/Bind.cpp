//===- Bind.cpp - Mockturtle Algorithm Bindings ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements mockturtle algorithm bindings that require RTTI and
// exception handling. These are isolated from the main codebase to avoid
// enabling exceptions globally.
//
//===----------------------------------------------------------------------===//

#include "../NetworkConversion.h"
#include "mockturtle/algorithms/aig_balancing.hpp"
#include "mockturtle/algorithms/aig_resub.hpp"
#include "mockturtle/algorithms/balancing.hpp"
#include "mockturtle/algorithms/balancing/sop_balancing.hpp"
#include "mockturtle/algorithms/functional_reduction.hpp"
#include "mockturtle/algorithms/lut_mapper.hpp"
#include "mockturtle/algorithms/mig_algebraic_rewriting.hpp"
#include "mockturtle/algorithms/mig_inv_optimization.hpp"
#include "mockturtle/algorithms/mig_inv_propagation.hpp"
#include "mockturtle/algorithms/mig_resub.hpp"
#include "mockturtle/algorithms/node_resynthesis/sop_factoring.hpp"
#include "mockturtle/algorithms/refactoring.hpp"
#include "mockturtle/algorithms/xag_algebraic_rewriting.hpp"
#include "mockturtle/algorithms/xag_balancing.hpp"
#include "mockturtle/algorithms/xag_resub.hpp"
#include "mockturtle/algorithms/xmg_algebraic_rewriting.hpp"
#include "mockturtle/algorithms/xmg_resub.hpp"
#include "mockturtle/networks/aig.hpp"
#include "mockturtle/networks/mig.hpp"
#include "mockturtle/networks/xag.hpp"
#include "mockturtle/networks/xmg.hpp"
#include "mockturtle/views/depth_view.hpp"
#include "mockturtle/views/fanout_view.hpp"
#include "llvm/Support/LogicalResult.h"
#include <functional>
#include <type_traits>
#include <variant>

static llvm::LogicalResult
catchException(const std::function<llvm::LogicalResult()> &func) {
  try {
    return func();
  } catch (const std::exception &e) {
    return llvm::failure();
  }
}

static mockturtle::resubstitution_params
getResubstitutionParams(const circt::mockturtle_plugin::mockturtle_integration::
                            ResubstitutionOptions &options) {
  mockturtle::resubstitution_params ps;
  ps.max_pis = options.maxPIs;
  ps.max_divisors = options.maxDivisors;
  ps.max_inserts = options.maxInserts;
  ps.skip_fanout_limit_for_roots = options.skipFanoutLimitForRoots;
  ps.skip_fanout_limit_for_divisors = options.skipFanoutLimitForDivisors;
  ps.progress = options.progress;
  ps.verbose = options.verbose;
  ps.use_dont_cares = options.useDontCares;
  ps.window_size = options.windowSize;
  ps.preserve_depth = options.preserveDepth;
  if (!options.patternFilename.empty())
    ps.pattern_filename = options.patternFilename;
  if (!options.savePatterns.empty())
    ps.save_patterns = options.savePatterns;
  ps.max_clauses = options.maxClauses;
  ps.conflict_limit = options.conflictLimit;
  ps.random_seed = options.randomSeed;
  ps.odc_levels = options.odcLevels;
  ps.max_trials = options.maxTrials;
  ps.max_divisors_k = options.maxDivisorsK;
  return ps;
}

static mockturtle::lut_map_params getLUTMapParams(
    const circt::mockturtle_plugin::mockturtle_integration::SOPBalancingOptions
        &options) {
  mockturtle::lut_map_params ps;
  ps.cut_enumeration_ps.cut_size = options.cutSize;
  ps.cut_enumeration_ps.cut_limit = options.cutLimit;
  ps.area_oriented_mapping = options.areaOrientedMapping;
  ps.required_delay = options.requiredDelay;
  ps.relax_required = options.relaxRequired;
  ps.recompute_cuts = options.recomputeCuts;
  ps.area_share_rounds = options.areaShareRounds;
  ps.area_flow_rounds = options.areaFlowRounds;
  ps.ela_rounds = options.elaRounds;
  ps.edge_optimization = options.edgeOptimization;
  ps.cut_expansion = options.cutExpansion;
  ps.remove_dominated_cuts = options.removeDominatedCuts;
  ps.cost_cache_vars = options.costCacheVars;
  ps.verbose = options.verbose;
  return ps;
}

llvm::LogicalResult
circt::mockturtle_plugin::mockturtle_integration::runSOPRefactoring(
    Ntk ntk, const RefactoringOptions &options) {
  return catchException([&]() {
    mockturtle::refactoring_params ps;
    ps.max_pis = options.maxPIs;
    ps.allow_zero_gain = options.allowZeroGain;
    ps.use_reconvergence_cut = options.useReconvergenceCut;
    ps.use_dont_cares = options.useDontCares;
    ps.progress = options.progress;
    ps.verbose = options.verbose;
    std::visit(
        [&](auto network) {
          using NetworkType = std::remove_pointer_t<decltype(network)>;
          mockturtle::sop_factoring<NetworkType> sop;
          mockturtle::refactoring(*network, sop, ps);
        },
        ntk);
    return llvm::success();
  });
}

llvm::LogicalResult
circt::mockturtle_plugin::mockturtle_integration::runFunctionalReduction(
    Ntk ntk, const FunctionalReductionOptions &options) {
  return catchException([&]() {
    mockturtle::functional_reduction_params ps;
    ps.progress = options.progress;
    ps.verbose = options.verbose;
    ps.max_iterations = options.maxIterations;
    if (!options.patternFilename.empty())
      ps.pattern_filename = options.patternFilename;
    if (!options.savePatterns.empty())
      ps.save_patterns = options.savePatterns;
    ps.max_TFI_nodes = options.maxTFINodes;
    ps.skip_fanout_limit = options.skipFanoutLimit;
    ps.conflict_limit = options.conflictLimit;
    ps.max_clauses = options.maxClauses;
    ps.num_patterns = options.numPatterns;
    ps.max_patterns = options.maxPatterns;

    std::visit(
        [&](auto network) { mockturtle::functional_reduction(*network, ps); },
        ntk);
    return llvm::success();
  });
}

llvm::LogicalResult
circt::mockturtle_plugin::mockturtle_integration::runMIGAlgebraicRewriteDepth(
    mockturtle::mig_network &ntk,
    const MIGAlgebraicRewriteDepthOptions &options) {
  return catchException([&]() {
    mockturtle::mig_algebraic_depth_rewriting_params ps;
    if (options.strategy == "aggressive")
      ps.strategy =
          mockturtle::mig_algebraic_depth_rewriting_params::aggressive;
    else if (options.strategy == "selective")
      ps.strategy = mockturtle::mig_algebraic_depth_rewriting_params::selective;
    else
      ps.strategy = mockturtle::mig_algebraic_depth_rewriting_params::dfs;
    ps.overhead = options.overhead;
    ps.allow_area_increase = options.allowAreaIncrease;
    mockturtle::depth_view<mockturtle::mig_network> depthView{ntk};
    mockturtle::mig_algebraic_depth_rewriting(depthView, ps);
    return llvm::success();
  });
}

llvm::LogicalResult
circt::mockturtle_plugin::mockturtle_integration::runSOPBalancing(
    Ntk ntk, const SOPBalancingOptions &options) {
  return catchException([&]() {
    auto ps = getLUTMapParams(options);
    std::visit(
        [&](auto network) {
          *network = std::move(mockturtle::sop_balancing(*network, ps));
        },
        ntk);
    return llvm::success();
  });
}

llvm::LogicalResult
circt::mockturtle_plugin::mockturtle_integration::runESOPBalancing(
    Ntk ntk, const SOPBalancingOptions &options) {
  return catchException([&]() {
    auto ps = getLUTMapParams(options);
    std::visit(
        [&](auto network) {
          *network = std::move(mockturtle::esop_balancing(*network, ps));
        },
        ntk);
    return llvm::success();
  });
}

llvm::LogicalResult
circt::mockturtle_plugin::mockturtle_integration::runAIGBalancing(
    mockturtle::aig_network &ntk, const AIGBalancingOptions &options) {
  return catchException([&]() {
    mockturtle::aig_balancing_params ps;
    ps.minimize_levels = options.minimizeLevels;
    ps.fast_mode = options.fastMode;
    mockturtle::aig_balance(ntk, ps);
    return llvm::success();
  });
}

llvm::LogicalResult
circt::mockturtle_plugin::mockturtle_integration::runXAGBalancing(
    mockturtle::xag_network &ntk, const AIGBalancingOptions &options) {
  return catchException([&]() {
    mockturtle::xag_balancing_params ps;
    ps.minimize_levels = options.minimizeLevels;
    ps.fast_mode = options.fastMode;
    mockturtle::xag_balance(ntk, ps);
    return llvm::success();
  });
}

llvm::LogicalResult
circt::mockturtle_plugin::mockturtle_integration::runAIGResubstitution(
    mockturtle::aig_network &ntk, const ResubstitutionOptions &options) {
  return catchException([&]() {
    auto ps = getResubstitutionParams(options);
    mockturtle::aig_resubstitution(ntk, ps);
    return llvm::success();
  });
}

llvm::LogicalResult
circt::mockturtle_plugin::mockturtle_integration::runAIGResubstitution2(
    mockturtle::aig_network &ntk, const ResubstitutionOptions &options) {
  return catchException([&]() {
    auto ps = getResubstitutionParams(options);
    mockturtle::depth_view<mockturtle::aig_network> depthView{ntk};
    mockturtle::fanout_view<decltype(depthView)> fanoutView{depthView};
    mockturtle::aig_resubstitution2(fanoutView, ps);
    return llvm::success();
  });
}

llvm::LogicalResult
circt::mockturtle_plugin::mockturtle_integration::runXAGResubstitution(
    mockturtle::xag_network &ntk, const ResubstitutionOptions &options) {
  return catchException([&]() {
    auto ps = getResubstitutionParams(options);
    mockturtle::depth_view<mockturtle::xag_network> depthView{ntk};
    mockturtle::fanout_view<decltype(depthView)> fanoutView{depthView};
    mockturtle::xag_resubstitution(fanoutView, ps);
    return llvm::success();
  });
}

llvm::LogicalResult
circt::mockturtle_plugin::mockturtle_integration::runMIGResubstitution(
    mockturtle::mig_network &ntk, const ResubstitutionOptions &options) {
  return catchException([&]() {
    auto ps = getResubstitutionParams(options);
    mockturtle::depth_view<mockturtle::mig_network> depthView{ntk};
    mockturtle::fanout_view<decltype(depthView)> fanoutView{depthView};
    mockturtle::mig_resubstitution(fanoutView, ps);
    return llvm::success();
  });
}

llvm::LogicalResult
circt::mockturtle_plugin::mockturtle_integration::runMIGResubstitution2(
    mockturtle::mig_network &ntk, const ResubstitutionOptions &options) {
  return catchException([&]() {
    auto ps = getResubstitutionParams(options);
    mockturtle::depth_view<mockturtle::mig_network> depthView{ntk};
    mockturtle::fanout_view<decltype(depthView)> fanoutView{depthView};
    mockturtle::mig_resubstitution2(fanoutView, ps);
    return llvm::success();
  });
}

llvm::LogicalResult
circt::mockturtle_plugin::mockturtle_integration::runXMGResubstitution(
    mockturtle::xmg_network &ntk, const ResubstitutionOptions &options) {
  return catchException([&]() {
    auto ps = getResubstitutionParams(options);
    mockturtle::xmg_resubstitution(ntk, ps);
    return llvm::success();
  });
}

llvm::LogicalResult
circt::mockturtle_plugin::mockturtle_integration::runXAGAlgebraicRewriteDepth(
    mockturtle::xag_network &ntk,
    const MIGAlgebraicRewriteDepthOptions &options) {
  return catchException([&]() {
    mockturtle::xag_algebraic_depth_rewriting_params ps;
    if (options.strategy == "aggressive")
      ps.strategy =
          mockturtle::xag_algebraic_depth_rewriting_params::aggressive;
    else if (options.strategy == "selective")
      ps.strategy = mockturtle::xag_algebraic_depth_rewriting_params::selective;
    else
      ps.strategy = mockturtle::xag_algebraic_depth_rewriting_params::dfs;
    ps.overhead = options.overhead;
    ps.allow_area_increase = options.allowAreaIncrease;
    mockturtle::depth_view<mockturtle::xag_network> depthView{ntk};
    mockturtle::xag_algebraic_depth_rewriting(depthView, ps);
    return llvm::success();
  });
}

llvm::LogicalResult
circt::mockturtle_plugin::mockturtle_integration::runXMGAlgebraicRewriteDepth(
    mockturtle::xmg_network &ntk,
    const MIGAlgebraicRewriteDepthOptions &options) {
  return catchException([&]() {
    mockturtle::xmg_algebraic_depth_rewriting_params ps;
    if (options.strategy == "aggressive")
      ps.strategy =
          mockturtle::xmg_algebraic_depth_rewriting_params::aggressive;
    else if (options.strategy == "selective")
      ps.strategy = mockturtle::xmg_algebraic_depth_rewriting_params::selective;
    else
      ps.strategy = mockturtle::xmg_algebraic_depth_rewriting_params::dfs;
    ps.overhead = options.overhead;
    ps.allow_area_increase = options.allowAreaIncrease;
    mockturtle::depth_view<mockturtle::xmg_network> depthView{ntk};
    mockturtle::xmg_algebraic_depth_rewriting(depthView, ps);
    return llvm::success();
  });
}

llvm::LogicalResult
circt::mockturtle_plugin::mockturtle_integration::runMIGInverterPropagation(
    mockturtle::mig_network &ntk) {
  return catchException([&]() {
    mockturtle::mig_inv_propagation(ntk);
    return llvm::success();
  });
}

llvm::LogicalResult
circt::mockturtle_plugin::mockturtle_integration::runMIGInverterOptimization(
    mockturtle::mig_network &ntk) {
  return catchException([&]() {
    mockturtle::fanout_view<mockturtle::mig_network> fanoutView{ntk};
    mockturtle::mig_inv_optimization(fanoutView);
    return llvm::success();
  });
}
