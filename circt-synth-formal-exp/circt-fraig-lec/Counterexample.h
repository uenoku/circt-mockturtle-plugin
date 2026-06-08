//===- Counterexample.h - FRAIG LEC counterexample support -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef CIRCT_FRAIG_LEC_COUNTEREXAMPLE_H
#define CIRCT_FRAIG_LEC_COUNTEREXAMPLE_H

#include "circt/Dialect/HW/HWOps.h"
#include "mlir/Support/LLVM.h"
#include "mlir/Support/LogicalResult.h"

namespace circt {
namespace fraig_lec {

mlir::LogicalResult printCounterexample(hw::HWModuleOp miter,
                                        mlir::Value property,
                                        llvm::StringRef satSolver,
                                        int64_t conflictLimit,
                                        llvm::raw_ostream &os);

} // namespace fraig_lec
} // namespace circt

#endif // CIRCT_FRAIG_LEC_COUNTEREXAMPLE_H
