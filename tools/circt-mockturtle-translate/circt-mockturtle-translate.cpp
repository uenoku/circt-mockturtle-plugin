//===- circt-mockturtle-translate.cpp - Translation Driver ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CIRCTMockturtle/CIRCTMockturtleTranslations.h"
#include "circt/Support/Version.h"
#include "mlir/Support/LogicalResult.h"
#include "mlir/Tools/mlir-translate/MlirTranslateMain.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/raw_ostream.h"

int main(int argc, char **argv) {
  llvm::setBugReportMsg(circt::circtBugReportMsg);
  circt::mockturtle_plugin::registerTranslations();
  llvm::cl::AddExtraVersionPrinter(
      [](llvm::raw_ostream &os) { os << circt::getCirctVersion() << '\n'; });
  return mlir::failed(mlir::mlirTranslateMain(
      argc, argv, "CIRCT mockturtle translation driver"));
}
