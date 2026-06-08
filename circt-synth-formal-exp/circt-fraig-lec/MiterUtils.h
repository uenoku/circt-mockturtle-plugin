//===- MiterUtils.h - FRAIG LEC shared utilities ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef CIRCT_FRAIG_LEC_MITERUTILS_H
#define CIRCT_FRAIG_LEC_MITERUTILS_H

#include "circt/Dialect/HW/HWOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Support/LLVM.h"
#include "mlir/Support/LogicalResult.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"

namespace circt {
namespace fraig_lec {

inline constexpr llvm::StringLiteral kMiterAttrName = "fraig_lec.miter";
inline constexpr llvm::StringLiteral kMiterKindAttrName =
    "fraig_lec.miter_kind";
inline constexpr llvm::StringLiteral kSideAttrName = "fraig_lec.side";
inline constexpr llvm::StringLiteral kPathAttrName = "fraig_lec.path";
inline constexpr llvm::StringLiteral kCheckNamesAttrName =
    "fraig_lec.check_names";
inline constexpr llvm::StringLiteral kCounterexampleNamesAttrName =
    "fraig_lec.counterexample_names";

struct MatchedRegister {
  mlir::StringAttr name;
  mlir::Type type;
};

bool isMiter(hw::HWModuleOp module);
bool isSupportedRegister(mlir::Operation *op);
bool isSupportedStatelessSeqOp(mlir::Operation *op);
bool isSeqOp(mlir::Operation *op);
std::optional<llvm::StringRef> getRegisterName(mlir::Operation *op);
mlir::Type getRegisterDataType(mlir::Operation *op);
mlir::StringAttr getHierarchicalRegisterName(mlir::Operation *op,
                                             mlir::OpBuilder &builder,
                                             llvm::StringRef pathPrefix);
mlir::StringAttr getMiterLocalRegisterName(mlir::Operation *op,
                                           mlir::OpBuilder &builder);
std::string appendInstancePath(llvm::StringRef pathPrefix,
                               llvm::StringRef instName);
std::string getStatePortName(mlir::StringAttr regName, llvm::StringRef side);
mlir::Value lookupMapped(mlir::IRMapping &mapping, mlir::Value value);
mlir::FailureOr<hw::HWModuleOp>
resolveInstanceModule(mlir::SymbolTable &symbolTable, hw::InstanceOp inst);
mlir::Value createEffectiveNextValue(mlir::OpBuilder &builder,
                                     mlir::Operation *op,
                                     mlir::IRMapping &mapping,
                                     mlir::Value state);
mlir::FailureOr<mlir::Value> createOutputMismatch(mlir::OpBuilder &builder,
                                                  mlir::Location loc,
                                                  mlir::Value lhs,
                                                  mlir::Value rhs);
mlir::Value lookupMiterInput(hw::HWModuleOp miter, llvm::StringRef portName);
mlir::LogicalResult appendToFail(hw::HWModuleOp miter, mlir::Value mismatch);
void appendCheckOutput(hw::HWModuleOp miter, mlir::StringRef label,
                       mlir::Value mismatch);
void setMiterLocalAttrs(mlir::Operation *op, mlir::OpBuilder &builder,
                        llvm::StringRef side, llvm::StringRef pathPrefix);

} // namespace fraig_lec
} // namespace circt

#endif // CIRCT_FRAIG_LEC_MITERUTILS_H
