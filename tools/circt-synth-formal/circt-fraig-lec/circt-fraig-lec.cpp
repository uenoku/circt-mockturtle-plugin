//===- circt-fraig-lec.cpp -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "BMCPDR.h"
#include "BTOR2Importer.h"
#include "Counterexample.h"
#include "MiterUtils.h"
#include "Passes.h"
#include "circt/Conversion/CombToSynth.h"
#include "circt/Dialect/Comb/CombDialect.h"
#include "circt/Dialect/HW/HWDialect.h"
#include "circt/Dialect/HW/HWOps.h"
#include "circt/Dialect/HW/HWPasses.h"
#include "circt/Dialect/Seq/SeqDialect.h"
#include "circt/Dialect/Synth/SynthDialect.h"
#include "circt/Dialect/Synth/Transforms/SynthPasses.h"
#include "circt/Dialect/Verif/VerifDialect.h"
#include "circt/Dialect/Verif/VerifOps.h"
#include "circt/Support/Passes.h"
#include "circt/Support/Version.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinDialect.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/Matchers.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Parser/Parser.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Support/FileUtilities.h"
#include "mlir/Support/LogicalResult.h"
#include "mlir/Transforms/Passes.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/ToolOutputFile.h"

using namespace mlir;
using namespace circt;

namespace cl = llvm::cl;

static cl::OptionCategory mainCategory("circt-fraig-lec Options");

static cl::opt<std::string> inputFilename(cl::Positional, cl::Required,
                                          cl::desc("<input file>"),
                                          cl::cat(mainCategory));

static cl::opt<std::string> outputFilename("o", cl::desc("Output filename"),
                                           cl::value_desc("filename"),
                                           cl::init("-"),
                                           cl::cat(mainCategory));

enum InputFormat { InputAuto, InputMLIR, InputBTOR2 };
static cl::opt<InputFormat> inputFormat(
    "input-format", cl::desc("Specify input format"),
    cl::values(clEnumValN(InputAuto, "auto", "Infer from file extension"),
               clEnumValN(InputMLIR, "mlir", "Parse MLIR"),
               clEnumValN(InputBTOR2, "btor2", "Parse BTOR2")),
    cl::init(InputAuto), cl::cat(mainCategory));

static cl::opt<unsigned>
    btor2Bound("btor2-bound",
               cl::desc("Bound used when materializing BTOR2 as verif.bmc"),
               cl::init(2), cl::cat(mainCategory));

static cl::opt<bool>
    btor2Sweep("btor2-sweep",
               cl::desc("For BTOR2 checking, run every bound from 1 through "
                        "--btor2-bound and stop at the first failing bound"),
               cl::init(false), cl::cat(mainCategory));

static cl::opt<unsigned>
    btor2PDRDepth("btor2-pdr-depth",
                  cl::desc("Run the prototype synth-netlist PDR engine after "
                           "BTOR2 import; 0 disables it"),
                  cl::init(0), cl::cat(mainCategory));

static cl::opt<unsigned>
    btor2PDRPrecheckDepth("btor2-pdr-precheck-depth",
                          cl::desc("Bounded precheck depth before BTOR2 PDR; "
                                   "0 uses --btor2-pdr-depth"),
                          cl::init(0), cl::cat(mainCategory));

static cl::opt<unsigned> btor2PDRDivModUnknownBits(
    "btor2-pdr-divmod-unknown-bits",
    cl::desc("Maximum unknown operand bits to emulate for div/mod in BTOR2 "
             "PDR normalization"),
    cl::init(16), cl::cat(mainCategory));

static cl::opt<unsigned> btor2PDRBlockedCubeLimit(
    "btor2-pdr-blocked-cube-limit",
    cl::desc("Maximum blocked cubes for BTOR2 PDR before returning bounded "
             "unknown; 0 disables the limit"),
    cl::init(0), cl::cat(mainCategory));

static cl::opt<bool>
    bmcKInduction("bmc-k-induction",
                  cl::desc("Also build a k-induction step miter for each "
                           "verif.bmc operation"),
                  cl::init(false), cl::cat(mainCategory));

static cl::opt<bool>
    bmcRecurrence("bmc-recurrence",
                  cl::desc("Also build a recurrence-diameter completeness "
                           "miter for each verif.bmc operation"),
                  cl::init(false), cl::cat(mainCategory));

enum OutputMode {
  CheckOnly,
  EmitImported,
  EmitMiter,
  EmitReduced,
  EmitPDRTransition,
  EmitPDRNormalizedTransition
};
static cl::opt<OutputMode> outputMode(
    cl::desc("Specify output mode"),
    cl::values(clEnumValN(CheckOnly, "check", "Run the proof flow"),
               clEnumValN(EmitImported, "emit-imported",
                          "Print parsed/imported input before miter lowering"),
               clEnumValN(EmitMiter, "emit-miter",
                          "Print the generated HW miter"),
               clEnumValN(EmitReduced, "emit-reduced",
                          "Print the reduced miter before checking"),
               clEnumValN(EmitPDRTransition, "emit-pdr-transition",
                          "Print the lowered PDR transition module"),
               clEnumValN(EmitPDRNormalizedTransition,
                          "emit-pdr-normalized-transition",
                          "Print the normalized PDR transition module")),
    cl::init(CheckOnly), cl::cat(mainCategory));

enum StopAfter {
  NoStop,
  StopAfterLower,
  StopAfterMatch,
  StopAfterInline,
  StopAfterPrune,
};
static cl::opt<StopAfter> stopAfter(
    "stop-after", cl::desc("Stop after a FRAIG LEC construction stage"),
    cl::values(clEnumValN(NoStop, "none", "Run the selected output mode"),
               clEnumValN(StopAfterLower, "lower", "Stop after miter lowering"),
               clEnumValN(StopAfterMatch, "match",
                          "Stop after the first register matching pass"),
               clEnumValN(StopAfterInline, "inline",
                          "Stop after the first instance inlining pass"),
               clEnumValN(StopAfterPrune, "prune",
                          "Stop after pruning helper modules")),
    cl::init(NoStop), cl::cat(mainCategory));

static cl::opt<unsigned> inlineIterations(
    "inline-iterations",
    cl::desc("Maximum gradual inline/match/cleanup iterations"), cl::init(16),
    cl::cat(mainCategory));

static cl::opt<bool>
    verifyPasses("verify-each",
                 cl::desc("Run the verifier after each transformation pass"),
                 cl::init(true), cl::cat(mainCategory));

static cl::opt<unsigned> numRandomPatterns(
    "num-random-patterns",
    cl::desc("Number of random simulation patterns for functional reduction"),
    cl::init(1024), cl::cat(mainCategory));

static cl::opt<unsigned> seed("seed", cl::desc("Random simulation seed"),
                              cl::init(0), cl::cat(mainCategory));

static cl::opt<std::string>
    satSolver("sat-solver",
              cl::desc("SAT solver backend for functional reduction"),
              cl::init("auto"), cl::cat(mainCategory));

static cl::opt<int64_t>
    conflictLimit("conflict-limit",
                  cl::desc("Per-SAT-call conflict budget; -1 disables it"),
                  cl::init(-1), cl::cat(mainCategory));

static cl::opt<bool>
    emitCounterexamples("counterexample",
                        cl::desc("Print SAT counterexamples for not-proven "
                                 "miter checks after reduction"),
                        cl::init(false), cl::cat(mainCategory));

static bool isConstantFalse(Value value) {
  APInt constant;
  return matchPattern(value, m_ConstantInt(&constant)) && constant.isZero();
}

static LogicalResult maybePrintCounterexample(hw::HWModuleOp miter,
                                              Value property, raw_ostream &os) {
  if (!emitCounterexamples)
    return success();
  return fraig_lec::printCounterexample(miter, property, satSolver,
                                        conflictLimit, os);
}

static FailureOr<bool> checkMiters(ModuleOp module, raw_ostream &os) {
  bool sawMiter = false;
  bool allProven = true;
  for (auto miter : module.getOps<hw::HWModuleOp>()) {
    if (!fraig_lec::isMiter(miter))
      continue;
    sawMiter = true;
    auto output = dyn_cast<hw::OutputOp>(miter.getBodyBlock()->getTerminator());
    if (!output || output.getOutputs().empty()) {
      miter.emitError() << "expected at least one fail output";
      return failure();
    }
    bool failProven = isConstantFalse(output.getOutputs().front());
    auto kind = miter->getAttrOfType<StringAttr>(fraig_lec::kMiterKindAttrName);
    StringRef provenLabel = "equivalent";
    if (kind && kind.getValue() == "bmc")
      provenLabel = "safe within bound";
    else if (kind && kind.getValue() == "bmc_induction")
      provenLabel = "inductive step proven";
    else if (kind && kind.getValue() == "bmc_recurrence")
      provenLabel = "complete within bound";
    os << miter.getName() << ": " << (failProven ? provenLabel : "not proven")
       << "\n";
    allProven &= failProven;

    auto checkNames =
        miter->getAttrOfType<ArrayAttr>(fraig_lec::kCheckNamesAttrName);
    ValueRange checks = output.getOutputs().drop_front();
    if (!failProven && checks.empty())
      if (failed(
              maybePrintCounterexample(miter, output.getOutputs().front(), os)))
        return failure();
    for (auto [index, check] : llvm::enumerate(checks)) {
      std::string label = ("check " + Twine(index)).str();
      if (checkNames && index < checkNames.size())
        if (auto labelAttr = dyn_cast<StringAttr>(checkNames[index]))
          label = labelAttr.getValue().str();
      bool checkProven = isConstantFalse(check);
      os << "  " << label << ": " << (checkProven ? "proven" : "not proven")
         << "\n";
      if (!checkProven)
        if (failed(maybePrintCounterexample(miter, check, os)))
          return failure();
      allProven &= checkProven;
    }
  }
  if (!sawMiter) {
    module.emitError("no miter modules were produced");
    return failure();
  }
  return allProven;
}

static LogicalResult printModule(ModuleOp module) {
  std::string errorMessage;
  auto output = openOutputFile(outputFilename, &errorMessage);
  if (!output) {
    llvm::errs() << errorMessage << "\n";
    return failure();
  }
  module.print(output->os());
  output->keep();
  return success();
}

static void addCleanup(PassManager &pm) {
  pm.addPass(createCSEPass());
  pm.addPass(createSimpleCanonicalizerPass());
  pm.addPass(createCSEPass());
}

static void addGradualMiterPipeline(PassManager &pm) {
  pm.addPass(fraig_lec::createLowerLECToMiter());
  pm.addPass(fraig_lec::createLowerBMCToMiter());
  if (stopAfter == StopAfterLower)
    return;

  pm.addPass(fraig_lec::createMatchRegisters());
  addCleanup(pm);
  if (stopAfter == StopAfterMatch)
    return;

  for (unsigned i = 0; i < inlineIterations; ++i) {
    pm.addPass(fraig_lec::createInlineInstanceLayer());
    pm.addPass(fraig_lec::createMatchRegisters());
    addCleanup(pm);
    if (stopAfter == StopAfterInline)
      return;
  }

  pm.addPass(fraig_lec::createPruneNonMiterModules());
  if (stopAfter == StopAfterPrune)
    return;
}

static void markBMCsForKInduction(ModuleOp module) {
  if (!bmcKInduction && !bmcRecurrence)
    return;
  Builder builder(module.getContext());
  module.walk([&](verif::BoundedModelCheckingOp op) {
    if (bmcKInduction)
      op->setAttr("fraig_lec.k_induction", builder.getUnitAttr());
    if (bmcRecurrence)
      op->setAttr("fraig_lec.recurrence", builder.getUnitAttr());
  });
}

static FailureOr<bool> runPipeline(ModuleOp module, raw_ostream &checkOS) {
  markBMCsForKInduction(module);

  PassManager pm(module.getContext());
  pm.enableVerifier(verifyPasses);
  if (failed(applyPassManagerCLOptions(pm)))
    return failure();

  addGradualMiterPipeline(pm);

  if (stopAfter == NoStop && outputMode != EmitMiter) {
    pm.addNestedPass<hw::HWModuleOp>(hw::createHWAggregateToComb());
    addCleanup(pm);

    ConvertCombToSynthOptions combToSynthOptions;
    combToSynthOptions.forceAIG = true;
    pm.addPass(createConvertCombToSynth(combToSynthOptions));
    pm.addPass(synth::createLowerWordToBits());
    addCleanup(pm);

    synth::FunctionalReductionOptions options;
    options.numRandomPatterns = numRandomPatterns;
    options.seed = seed;
    options.satSolver = satSolver;
    options.conflictLimit = conflictLimit;
    pm.addNestedPass<hw::HWModuleOp>(
        synth::createFunctionalReduction(std::move(options)));

    pm.addPass(fraig_lec::createResolveChoices());
    addCleanup(pm);
  }

  if (failed(pm.run(module)))
    return failure();

  if (stopAfter != NoStop || outputMode == EmitMiter ||
      outputMode == EmitReduced)
    if (failed(printModule(module)))
      return failure();

  if (stopAfter != NoStop || outputMode == EmitMiter)
    return true;

  return checkMiters(module, checkOS);
}

static FailureOr<bool> runBTOR2Sweep(MLIRContext &context) {
  if (outputMode != CheckOnly || stopAfter != NoStop) {
    llvm::errs() << "--btor2-sweep only supports check mode without "
                    "--stop-after\n";
    return failure();
  }

  for (unsigned bound = 1; bound <= btor2Bound; ++bound) {
    auto imported = fraig_lec::parseBTOR2File(inputFilename, context, bound);
    if (failed(imported))
      return failure();

    llvm::outs() << "bound " << bound << ":\n";
    auto proven = runPipeline(imported->get(), llvm::outs());
    if (failed(proven))
      return failure();
    if (!*proven) {
      llvm::outs() << "first failing bound: " << bound << "\n";
      return false;
    }
  }

  llvm::outs() << "safe within bound " << btor2Bound << "\n";
  return true;
}

static FailureOr<bool> runBTOR2BMCPrecheck(MLIRContext &context,
                                           unsigned maxBound) {
  auto imported =
      fraig_lec::parseBTOR2File(inputFilename, context, /*bound=*/1);
  if (failed(imported))
    return failure();
  return fraig_lec::runBMCPrecheck(imported->get(), satSolver, maxBound,
                                   llvm::outs(), btor2PDRDivModUnknownBits,
                                   conflictLimit);
}

static LogicalResult run(MLIRContext &context) {
  if (btor2PDRDivModUnknownBits >= 32) {
    llvm::errs() << "--btor2-pdr-divmod-unknown-bits must be less than 32\n";
    return failure();
  }

  InputFormat resolvedInputFormat = inputFormat;
  if (resolvedInputFormat == InputAuto)
    resolvedInputFormat =
        StringRef(inputFilename).ends_with_insensitive(".btor2") ||
                StringRef(inputFilename).ends_with_insensitive(".btor")
            ? InputBTOR2
            : InputMLIR;

  if (resolvedInputFormat == InputBTOR2 && btor2Sweep) {
    auto proven = runBTOR2Sweep(context);
    if (failed(proven))
      return failure();
    return success(*proven);
  }

  if (resolvedInputFormat == InputBTOR2 && btor2PDRDepth != 0 &&
      outputMode == CheckOnly) {
    unsigned precheckDepth =
        btor2PDRPrecheckDepth ? btor2PDRPrecheckDepth : btor2PDRDepth;
    auto bmcSafe = runBTOR2BMCPrecheck(context, precheckDepth);
    if (failed(bmcSafe))
      return failure();
    if (!*bmcSafe)
      return success(false);

    auto imported =
        fraig_lec::parseBTOR2File(inputFilename, context, /*bound=*/1);
    if (failed(imported))
      return failure();
    auto proven = fraig_lec::runBMCPDR(
        imported->get(), satSolver, btor2PDRDepth, llvm::outs(),
        /*useInitialImage=*/true, btor2PDRDivModUnknownBits,
        btor2PDRBlockedCubeLimit, conflictLimit);
    if (failed(proven))
      return failure();
    return success(*proven);
  }

  OwningOpRef<ModuleOp> module;
  if (resolvedInputFormat == InputBTOR2) {
    auto imported =
        fraig_lec::parseBTOR2File(inputFilename, context, btor2Bound);
    if (failed(imported))
      return failure();
    module = std::move(*imported);
  } else {
    module = parseSourceFile<ModuleOp>(inputFilename, &context);
  }
  if (!module)
    return failure();

  if (outputMode == EmitImported)
    return printModule(module.get());
  if (outputMode == EmitPDRTransition ||
      outputMode == EmitPDRNormalizedTransition)
    return fraig_lec::printPDRTransition(module.get(), llvm::outs(),
                                         /*normalize=*/outputMode ==
                                             EmitPDRNormalizedTransition,
                                         btor2PDRDivModUnknownBits);

  auto proven = runPipeline(
      module.get(), outputMode == CheckOnly ? llvm::outs() : llvm::errs());
  if (failed(proven))
    return failure();
  return success(*proven);
}

int main(int argc, char **argv) {
  llvm::InitLLVM y(argc, argv);
  llvm::setBugReportMsg(circt::circtBugReportMsg);

  cl::HideUnrelatedOptions(mainCategory);
  registerMLIRContextCLOptions();
  registerPassManagerCLOptions();
  registerAsmPrinterCLOptions();
  cl::AddExtraVersionPrinter(
      [](llvm::raw_ostream &os) { os << circt::getCirctVersion() << '\n'; });
  cl::ParseCommandLineOptions(
      argc, argv,
      "circt-fraig-lec - prove verif.lec with an HW miter and synth FRAIG\n");

  DialectRegistry registry;
  registry.insert<mlir::BuiltinDialect, circt::comb::CombDialect,
                  circt::hw::HWDialect, circt::seq::SeqDialect,
                  circt::synth::SynthDialect, circt::verif::VerifDialect>();
  MLIRContext context(registry);
  context.getOrLoadDialect<circt::comb::CombDialect>();
  context.getOrLoadDialect<circt::hw::HWDialect>();
  context.getOrLoadDialect<circt::seq::SeqDialect>();
  context.getOrLoadDialect<circt::synth::SynthDialect>();
  context.getOrLoadDialect<circt::verif::VerifDialect>();
  context.printOpOnDiagnostic(false);

  llvm::SourceMgr sourceMgr;
  SourceMgrDiagnosticHandler sourceMgrHandler(sourceMgr, &context);

  return failed(run(context));
}
