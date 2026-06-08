//===- BTOR2Importer.h - Materialize BTOR2 as verif.bmc --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef CIRCT_FRAIG_LEC_BTOR2IMPORTER_H
#define CIRCT_FRAIG_LEC_BTOR2IMPORTER_H

#include "mlir/IR/BuiltinOps.h"
#include "mlir/Support/LLVM.h"
#include "mlir/Support/LogicalResult.h"
#include "llvm/ADT/StringRef.h"

namespace circt {
namespace fraig_lec {

mlir::FailureOr<mlir::OwningOpRef<mlir::ModuleOp>>
parseBTOR2File(llvm::StringRef filename, mlir::MLIRContext &context,
               unsigned bound);

} // namespace fraig_lec
} // namespace circt

#endif // CIRCT_FRAIG_LEC_BTOR2IMPORTER_H
