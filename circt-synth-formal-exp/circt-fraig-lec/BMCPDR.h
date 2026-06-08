//===- BMCPDR.h - Minimal verif.bmc PDR engine -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef CIRCT_FRAIG_LEC_BMCPDR_H
#define CIRCT_FRAIG_LEC_BMCPDR_H

#include "mlir/IR/BuiltinOps.h"
#include "mlir/Support/LLVM.h"
#include "mlir/Support/LogicalResult.h"
#include "llvm/ADT/StringRef.h"

namespace circt {
namespace fraig_lec {

mlir::FailureOr<bool>
runBMCPDR(mlir::ModuleOp module, llvm::StringRef satSolver, unsigned maxDepth,
          llvm::raw_ostream &os, bool useInitialImage = false,
          unsigned maxDivModUnknownBits = 16, unsigned maxBlockedCubes = 0,
          int64_t conflictLimit = -1);

mlir::FailureOr<bool> runBMCPrecheck(mlir::ModuleOp module,
                                     llvm::StringRef satSolver,
                                     unsigned maxBound, llvm::raw_ostream &os,
                                     unsigned maxDivModUnknownBits = 16,
                                     int64_t conflictLimit = -1);

mlir::LogicalResult printPDRTransition(mlir::ModuleOp module,
                                       llvm::raw_ostream &os,
                                       bool normalize = false,
                                       unsigned maxDivModUnknownBits = 16);

} // namespace fraig_lec
} // namespace circt

#endif // CIRCT_FRAIG_LEC_BMCPDR_H
