//===- NetworkConversion.h - Mockturtle Integration -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides high-level interfaces for applying mockturtle algorithms
// to CIRCT IR. It handles the conversion between MLIR and mockturtle networks.
//
//===----------------------------------------------------------------------===//

#ifndef LIB_DIALECT_SYNTH_TRANSFORMS_MOCKTURTLEINTEGRATION_NETWORKCONVERSION_H
#define LIB_DIALECT_SYNTH_TRANSFORMS_MOCKTURTLEINTEGRATION_NETWORKCONVERSION_H

#include "mlir/IR/Operation.h"
#include "mlir/Support/LogicalResult.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/LogicalResult.h"
#include <string>
#include <variant>

// Forward declarations
namespace mlir {
class Block;
class SymbolTable;
} // namespace mlir

namespace mockturtle {
class aig_network;
class mig_network;
class xag_network;
class xmg_network;
template <typename Ntk>
class cell_view;
class block_network;
} // namespace mockturtle

namespace circt {
namespace mockturtle_plugin {
namespace mockturtle_integration {

/// Variant type representing different network types
using Ntk = std::variant<mockturtle::aig_network *, mockturtle::mig_network *>;

struct RefactoringOptions {
  uint32_t maxPIs = 6;
  bool allowZeroGain = false;
  bool useReconvergenceCut = true;
  bool useDontCares = false;
  bool progress = false;
  bool verbose = false;
};

struct FunctionalReductionOptions {
  bool progress = false;
  bool verbose = false;
  uint32_t maxIterations = 10;
  std::string patternFilename;
  std::string savePatterns;
  uint32_t maxTFINodes = 1000;
  uint32_t skipFanoutLimit = 100;
  uint32_t conflictLimit = 100;
  uint32_t maxClauses = 1000;
  uint32_t numPatterns = 256;
  uint32_t maxPatterns = 1024;
};

struct MIGAlgebraicRewriteDepthOptions {
  std::string strategy = "dfs";
  float overhead = 2.0f;
  bool allowAreaIncrease = true;
};

struct SOPBalancingOptions {
  uint32_t cutSize = 6;
  uint32_t cutLimit = 8;
  bool areaOrientedMapping = false;
  uint32_t requiredDelay = 0;
  uint32_t relaxRequired = 0;
  bool recomputeCuts = true;
  uint32_t areaShareRounds = 2;
  uint32_t areaFlowRounds = 1;
  uint32_t elaRounds = 2;
  bool edgeOptimization = true;
  bool cutExpansion = true;
  bool removeDominatedCuts = true;
  uint32_t costCacheVars = 3;
  bool verbose = false;
};

struct AIGBalancingOptions {
  bool minimizeLevels = true;
  bool fastMode = true;
};

struct ResubstitutionOptions {
  uint32_t maxPIs = 8;
  uint32_t maxDivisors = 150;
  uint32_t maxInserts = 2;
  uint32_t skipFanoutLimitForRoots = 1000;
  uint32_t skipFanoutLimitForDivisors = 100;
  bool progress = false;
  bool verbose = false;
  bool useDontCares = false;
  uint32_t windowSize = 12;
  bool preserveDepth = false;
  std::string patternFilename;
  std::string savePatterns;
  uint32_t maxClauses = 1000;
  uint32_t conflictLimit = 1000;
  uint32_t randomSeed = 1;
  int32_t odcLevels = 0;
  uint32_t maxTrials = 100;
  uint32_t maxDivisorsK = 50;
};

struct AIGToGraphConversionOptions {
  bool useMultiple = false;
  bool verbose = false;
};

//===----------------------------------------------------------------------===//
// High-level transformation interfaces
//===----------------------------------------------------------------------===//

/// Apply a transformation to an AIG network extracted from the block.
/// The network is constructed from and-inverter operations, transformed
/// in-place, and then written back to the block.
llvm::LogicalResult runAIGNetworkTransforms(
    mlir::Block *block,
    llvm::function_ref<llvm::LogicalResult(mockturtle::aig_network &)>
        transform);

/// Apply a transformation to a MIG network extracted from the block.
llvm::LogicalResult runMIGNetworkTransforms(
    mlir::Block *block,
    llvm::function_ref<llvm::LogicalResult(mockturtle::mig_network &)>
        transform);

/// Apply a transformation to an XAG network extracted from the block.
llvm::LogicalResult runXAGNetworkTransforms(
    mlir::Block *block,
    llvm::function_ref<llvm::LogicalResult(mockturtle::xag_network &)>
        transform);

/// Apply a transformation to an XMG network extracted from the block.
llvm::LogicalResult runXMGNetworkTransforms(
    mlir::Block *block,
    llvm::function_ref<llvm::LogicalResult(mockturtle::xmg_network &)>
        transform);

/// Convert an AIG network extracted from the block to XAG using mockturtle node
/// resynthesis.
llvm::LogicalResult
runAIGToXAGNetworkConversion(mlir::Block *block,
                             const AIGToGraphConversionOptions &options);

/// Convert an AIG network extracted from the block to MIG using mockturtle node
/// resynthesis.
llvm::LogicalResult
runAIGToMIGNetworkConversion(mlir::Block *block,
                             const AIGToGraphConversionOptions &options);

/// Convert an AIG network extracted from the block to XMG using mockturtle node
/// resynthesis.
llvm::LogicalResult
runAIGToXMGNetworkConversion(mlir::Block *block,
                             const AIGToGraphConversionOptions &options);

/// Apply a transformation that maps a network to cell-view representation.
/// This is used for technology mapping where the result is a set of
/// instantiated cells.
llvm::LogicalResult runAIGNetworkToCellView(
    const mlir::SymbolTable &symbolTable, mlir::Block *block,
    llvm::function_ref<
        mlir::FailureOr<mockturtle::cell_view<mockturtle::block_network>>(
            mockturtle::aig_network &)>
        mapFunction);

/// Apply a generic transformation to a network (auto-detects network type)
llvm::LogicalResult
runNetworkTransforms(mlir::Block *block,
                     llvm::function_ref<llvm::LogicalResult(Ntk &)> transform);
/// Apply a generic transformation to a network (auto-detects network type)
llvm::LogicalResult
runNetworkTransforms(mlir::Operation *op,
                     llvm::function_ref<llvm::LogicalResult(Ntk &)> transform);

//===----------------------------------------------------------------------===//
// Algorithm Bindings
//===----------------------------------------------------------------------===//

/// Run SOP refactoring on the network.
llvm::LogicalResult runSOPRefactoring(Ntk ntk,
                                      const RefactoringOptions &options);

/// Run functional reduction to identify and remove redundant nodes.
llvm::LogicalResult
runFunctionalReduction(Ntk ntk, const FunctionalReductionOptions &options);

/// Run MIG algebraic depth rewriting to optimize circuit depth
llvm::LogicalResult
runMIGAlgebraicRewriteDepth(mockturtle::mig_network &ntk,
                            const MIGAlgebraicRewriteDepthOptions &options);

/// Run SOP balancing to optimize circuit structure
llvm::LogicalResult runSOPBalancing(Ntk ntk,
                                    const SOPBalancingOptions &options);

/// Run ESOP balancing to optimize circuit structure
llvm::LogicalResult runESOPBalancing(Ntk ntk,
                                     const SOPBalancingOptions &options);

/// Run AIG tree balancing.
llvm::LogicalResult runAIGBalancing(mockturtle::aig_network &ntk,
                                    const AIGBalancingOptions &options);

/// Run XAG tree balancing.
llvm::LogicalResult runXAGBalancing(mockturtle::xag_network &ntk,
                                    const AIGBalancingOptions &options);

/// Run AIG-specific resubstitution.
llvm::LogicalResult runAIGResubstitution(mockturtle::aig_network &ntk,
                                         const ResubstitutionOptions &options);

/// Run window-based AIG-specific resubstitution.
llvm::LogicalResult runAIGResubstitution2(mockturtle::aig_network &ntk,
                                          const ResubstitutionOptions &options);

/// Run XAG-specific resubstitution.
llvm::LogicalResult runXAGResubstitution(mockturtle::xag_network &ntk,
                                         const ResubstitutionOptions &options);

/// Run MIG-specific resubstitution.
llvm::LogicalResult runMIGResubstitution(mockturtle::mig_network &ntk,
                                         const ResubstitutionOptions &options);

/// Run window-based MIG-specific resubstitution.
llvm::LogicalResult runMIGResubstitution2(mockturtle::mig_network &ntk,
                                          const ResubstitutionOptions &options);

/// Run XMG-specific resubstitution.
llvm::LogicalResult runXMGResubstitution(mockturtle::xmg_network &ntk,
                                         const ResubstitutionOptions &options);

/// Run XAG algebraic depth rewriting.
llvm::LogicalResult
runXAGAlgebraicRewriteDepth(mockturtle::xag_network &ntk,
                            const MIGAlgebraicRewriteDepthOptions &options);

/// Run XMG algebraic depth rewriting.
llvm::LogicalResult
runXMGAlgebraicRewriteDepth(mockturtle::xmg_network &ntk,
                            const MIGAlgebraicRewriteDepthOptions &options);

/// Run MIG inverter propagation.
llvm::LogicalResult runMIGInverterPropagation(mockturtle::mig_network &ntk);

/// Run MIG inverter optimization.
llvm::LogicalResult runMIGInverterOptimization(mockturtle::mig_network &ntk);

} // namespace mockturtle_integration
} // namespace mockturtle_plugin
} // namespace circt

#endif // LIB_DIALECT_SYNTH_TRANSFORMS_MOCKTURTLEINTEGRATION_NETWORKCONVERSION_H
