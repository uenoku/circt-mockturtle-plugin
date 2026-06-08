//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CIRCTSynthFormal/BTOR2Importer.h"
#include "mlir/Tools/mlir-translate/Translation.h"
#include "llvm/Support/CommandLine.h"

using namespace circt;
using namespace mlir;

namespace {
static llvm::cl::opt<unsigned> btor2TranslateBound(
    "btor2-bound",
    llvm::cl::desc("Bound used when materializing BTOR2 as verif.bmc"),
    llvm::cl::init(2));

static OwningOpRef<Operation *>
btor2ToMlirFunc(llvm::StringRef filename, MLIRContext *context) {
  auto result = fraig_lec::parseBTOR2File(filename, *context, btor2TranslateBound);
  if (failed(result))
    return {};
  return OwningOpRef<Operation *>((*result).release().getOperation());
}

static TranslateToMLIRRegistration btor2ToMlir(
    "btor2-to-mlir", "Import BTOR2 to MLIR", btor2ToMlirFunc);
} // namespace

namespace circt {
namespace synth_formal {

void registerTranslations() {}

} // namespace synth_formal
} // namespace circt
