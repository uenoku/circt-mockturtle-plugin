//===- BMCPDR.cpp - PDR over normalized BMC transition logic ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "BMCPDR.h"
#include "Passes.h"
#include "circt/Conversion/CombToSynth.h"
#include "circt/Dialect/Comb/CombOps.h"
#include "circt/Dialect/HW/HWOps.h"
#include "circt/Dialect/HW/HWPasses.h"
#include "circt/Dialect/HW/HWTypes.h"
#include "circt/Dialect/Seq/SeqOps.h"
#include "circt/Dialect/Synth/SynthOps.h"
#include "circt/Dialect/Synth/Transforms/SynthPasses.h"
#include "circt/Dialect/Verif/VerifOps.h"
#include "circt/Support/Passes.h"
#include "circt/Support/SATSolver.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/IR/Matchers.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Transforms/Passes.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/DebugLog.h"
#include <algorithm>
#include <cstdlib>
#include <limits>

#define DEBUG_TYPE "fraig-lec-pdr"

using namespace mlir;
using namespace circt;
using namespace circt::fraig_lec;

static constexpr StringRef kInitialOnlyAttr = "fraig_lec.initial_only";

namespace {

using Clause = SmallVector<int>;
using Cube = SmallVector<int>;

struct TransitionLoweringResult {
  hw::HWModuleOp module;
  SmallVector<std::optional<APInt>> initialValues;
  SmallVector<std::string> stateNames;
  SmallVector<std::string> inputNames;
};

struct StateInfo {
  BlockArgument input;
  Value next;
  std::string name;
  std::optional<APInt> initialValue;
  unsigned width = 0;
  unsigned offset = 0;
};

struct InputInfo {
  BlockArgument input;
  std::string name;
  unsigned width = 0;
};

struct InputAssignment {
  std::string name;
  unsigned width = 0;
  SmallVector<int> bits;
};

struct InputValue {
  std::string name;
  APInt value;
};

struct TransitionSystem {
  hw::HWModuleOp module;
  SmallVector<StateInfo> states;
  SmallVector<InputInfo> inputs;
  Value initial;
  Value constraint;
  Value bad;
  unsigned numStateBits = 0;
};

struct QueryResult {
  IncrementalSATSolver::Result result = IncrementalSATSolver::kUNKNOWN;
  Cube stateCube;
  Cube failedCube;
  SmallVector<InputValue> inputValues;
};

static void printNamedStateValue(raw_ostream &os, StringRef name,
                                 unsigned width, const APInt &value) {
  os << name << "=";
  if (width == 1) {
    os << (value[0] ? "1" : "0");
    return;
  }

  os << "0b";
  for (unsigned bit = width; bit != 0; --bit)
    os << (value[bit - 1] ? "1" : "0");
}

static std::unique_ptr<IncrementalSATSolver>
createSATSolver(StringRef solverName) {
  if (solverName == "z3")
    return createZ3SATSolver();
  if (solverName == "cadical")
    return createCadicalSATSolver();
  if (solverName != "auto")
    return {};
  if (auto solver = createCadicalSATSolver())
    return solver;
  return createZ3SATSolver();
}

static StringRef getSATResultName(IncrementalSATSolver::Result result) {
  switch (result) {
  case IncrementalSATSolver::kSAT:
    return "sat";
  case IncrementalSATSolver::kUNSAT:
    return "unsat";
  case IncrementalSATSolver::kUNKNOWN:
    return "unknown";
  }
  llvm_unreachable("unknown SAT result");
}

static unsigned countOperations(Operation *op) {
  unsigned count = 0;
  op->walk([&](Operation *) { ++count; });
  return count;
}

class QueryEncoder {
public:
  QueryEncoder(TransitionSystem &system, IncrementalSATSolver &solver,
               ArrayRef<int> currentVars = {}, ArrayRef<int> nextVars = {})
      : system(system), solver(solver) {
    currentStateVars.append(currentVars.begin(), currentVars.end());
    nextStateVars.append(nextVars.begin(), nextVars.end());
    for (unsigned i = 0, e = system.numStateBits; i != e; ++i) {
      if (currentStateVars.size() <= i)
        currentStateVars.push_back(solver.newVar());
      if (nextStateVars.size() <= i)
        nextStateVars.push_back(solver.newVar());
      setLitDeps(currentStateVars[i], ArrayRef<unsigned>{i});
    }
  }

  FailureOr<int> getBit(Value value, unsigned bit);
  void beginQuery() { usedCurrentStateBits = permanentCurrentStateBits; }
  LogicalResult encodeTransitionForCube(ArrayRef<int> cube);
  LogicalResult encodeInitial();
  LogicalResult encodeConstraint();
  LogicalResult encodeBad();
  void addFrameClause(ArrayRef<int> clause);
  void addConstraint() {
    markLitDeps(constraintLit);
    solver.addClause({constraintLit});
  }
  void addInitial() {
    markLitDeps(initialLit);
    solver.addClause({initialLit});
  }
  void assumeStateCube(ArrayRef<int> cube, bool next);
  void assumeInitial() {
    markLitDeps(initialLit);
    solver.assume(initialLit);
  }
  void assumeConstraint() {
    markLitDeps(constraintLit);
    solver.assume(constraintLit);
  }
  void assumeBad() {
    markLitDeps(badLit);
    solver.assume(badLit);
  }
  Cube getCurrentStateCube() const;
  Cube getFullCurrentStateCube() const;
  Cube getFailedStateCube(ArrayRef<int> cube, bool next) const;
  SmallVector<InputAssignment> getInputAssignments() const;
  SmallVector<InputValue> getInputValues() const;
  ArrayRef<int> getNextStateVars() const { return nextStateVars; }

private:
  FailureOr<SmallVector<int>> getBits(Value value);
  FailureOr<SmallVector<int>> getBlockArgumentBits(BlockArgument arg);
  FailureOr<SmallVector<int>> getConstantBits(Value value,
                                              const APInt &constant);
  FailureOr<SmallVector<int>> getConcatBits(comb::ConcatOp concat);
  FailureOr<SmallVector<int>> getExtractBits(comb::ExtractOp extract);
  FailureOr<SmallVector<int>> getReplicateBits(comb::ReplicateOp replicate);
  FailureOr<SmallVector<int>> getBitcastBits(hw::BitcastOp bitcast);
  FailureOr<SmallVector<int>>
  getBooleanLogicBits(synth::BooleanLogicOpInterface op);
  FailureOr<unsigned> getKnownBitWidth(Type type);
  void addEquivalence(int lhs, int rhs);
  const StateInfo *getStateInfo(BlockArgument arg) const;
  FailureOr<int> getStateBit(BlockArgument arg, unsigned bit);
  int mapStateLit(int lit, bool next) const;
  void setLitDeps(int lit, ArrayRef<unsigned> deps);
  void markLitDeps(int lit);
  SmallVector<unsigned> getLitDeps(int lit) const;
  SmallVector<unsigned> unionLitDeps(ArrayRef<int> lits) const;

  TransitionSystem &system;
  IncrementalSATSolver &solver;
  SmallVector<int> currentStateVars;
  SmallVector<int> nextStateVars;
  DenseMap<Value, SmallVector<int>> encodedBits;
  DenseSet<Value> encodingValues;
  DenseMap<BlockArgument, SmallVector<int>> inputBits;
  DenseMap<int, SmallVector<unsigned>> literalStateDeps;
  DenseSet<unsigned> encodedNextStateBits;
  DenseSet<unsigned> permanentCurrentStateBits;
  DenseSet<unsigned> usedCurrentStateBits;
  int constraintLit = 0;
  int initialLit = 0;
  int badLit = 0;
};

struct FrameContext {
  std::unique_ptr<IncrementalSATSolver> solver;
  std::unique_ptr<QueryEncoder> encoder;
  unsigned numFrameClauses = 0;
};

struct InitialPredecessorContext {
  std::unique_ptr<IncrementalSATSolver> solver;
  std::unique_ptr<QueryEncoder> initialEncoder;
  std::unique_ptr<QueryEncoder> stepEncoder;
  unsigned numFrameClauses = 0;
};

class PDREngine {
public:
  PDREngine(TransitionSystem system, StringRef solverName, unsigned maxDepth,
            raw_ostream &os, bool useInitialImage, unsigned maxBlockedCubes,
            int64_t conflictLimit)
      : system(std::move(system)), solverName(solverName.str()),
        maxDepth(maxDepth), os(os), useInitialImage(useInitialImage),
        maxBlockedCubes(maxBlockedCubes), conflictLimit(conflictLimit) {}

  FailureOr<bool> run();

private:
  std::unique_ptr<IncrementalSATSolver> createSolver();
  FailureOr<FrameContext *> getFrameContext(unsigned frame);
  FailureOr<FrameContext *> getInitialImageContext();
  FailureOr<InitialPredecessorContext *> getInitialPredecessorContext();
  LogicalResult syncFrameClauses(unsigned frame);
  LogicalResult syncInitialPredecessorClauses();
  FailureOr<QueryResult> queryBad(unsigned frame);
  FailureOr<QueryResult> queryInitialCubeWithCore(ArrayRef<int> cube);
  FailureOr<IncrementalSATSolver::Result> queryInitialCube(ArrayRef<int> cube);
  FailureOr<QueryResult> queryInitialImageCubeWithCore(ArrayRef<int> cube);
  FailureOr<QueryResult> queryInitialImagePredecessor(ArrayRef<int> cube);
  FailureOr<QueryResult> queryPredecessor(unsigned frame, ArrayRef<int> cube);
  FailureOr<SmallVector<Clause>>
  computeInitialImageUnits(ArrayRef<Clause> initialClauses);
  FailureOr<SmallVector<Clause>>
  computeInductiveUnitInvariants(ArrayRef<Clause> initialImageUnits);
  FailureOr<SmallVector<Clause>> computeConstraintUnitInvariants();
  FailureOr<Cube> generalizeCubeWithCores(Cube cube, unsigned frame);
  FailureOr<Cube> generalizeCube(Cube cube, unsigned frame);
  FailureOr<std::optional<Clause>> propagateClause(unsigned frame,
                                                   const Clause &clause);
  LogicalResult propagateClauses(unsigned maxFrame);
  LogicalResult blockCube(Cube cube, unsigned frame);
  bool isFixedPoint(unsigned frame) const;
  std::optional<unsigned> findFixedPoint(unsigned maxFrame) const;
  void addBlockedCube(ArrayRef<int> cube, unsigned frame);
  void addFrameClause(unsigned frame, Clause clause);
  bool containsSubsumingClause(unsigned frame, const Clause &clause) const;
  bool subsumes(const Clause &lhs, const Clause &rhs) const;
  Clause negateCube(ArrayRef<int> cube) const;
  std::optional<APInt> getStateValueFromCube(const StateInfo &state,
                                             ArrayRef<int> cube) const;
  void printStateValue(raw_ostream &os, const StateInfo &state,
                       const APInt &value) const;
  void recordCounterexampleBase(ArrayRef<int> cube);
  void appendCounterexampleState(ArrayRef<int> cube,
                                 ArrayRef<InputValue> inputValues);
  void printCounterexampleTrace() const;
  void dumpFramesForTrace() const;
  void traceProgress(StringRef event, unsigned frame, unsigned cubeSize);
  bool reachedBlockedCubeLimit() const {
    return maxBlockedCubes != 0 && numBlockedCubes >= maxBlockedCubes;
  }
  void printBlockedCubeLimitUnknown(unsigned depth);
  void printConflictLimitUnknown(unsigned depth);

  TransitionSystem system;
  std::string solverName;
  unsigned maxDepth;
  raw_ostream &os;
  bool useInitialImage;
  unsigned maxBlockedCubes;
  int64_t conflictLimit;
  SmallVector<Clause> originalInitialClauses;
  SmallVector<SmallVector<Clause>> frames;
  SmallVector<std::unique_ptr<FrameContext>> frameContexts;
  std::unique_ptr<FrameContext> initialImageContext;
  std::unique_ptr<InitialPredecessorContext> initialPredecessorContext;
  SmallVector<Clause> globalInvariants;
  SmallVector<Cube> counterexampleStates;
  SmallVector<SmallVector<InputValue>> counterexampleInputs;
  bool foundCounterexample = false;
  bool constraintsEmpty = false;
  bool initialImageEmpty = false;
  uint64_t numBadQueries = 0;
  uint64_t numPredecessorQueries = 0;
  uint64_t numBlockedCubes = 0;
  uint64_t numUsedCores = 0;
  uint64_t numEmptyCores = 0;
  uint64_t numInitialWeakCores = 0;
};

} // namespace

static Value getTrue(OpBuilder &builder, Location loc) {
  return hw::ConstantOp::create(builder, loc, APInt(1, 1));
}

static Value getFalse(OpBuilder &builder, Location loc) {
  return hw::ConstantOp::create(builder, loc, APInt(1, 0));
}

static Value createNot(OpBuilder &builder, Location loc, Value value) {
  return builder.createOrFold<comb::XorOp>(loc, value, getTrue(builder, loc));
}

static Value createAnd(OpBuilder &builder, Location loc, Value lhs, Value rhs) {
  return builder.createOrFold<comb::AndOp>(loc, ValueRange{lhs, rhs}, true);
}

static Value createOr(OpBuilder &builder, Location loc, Value lhs, Value rhs) {
  return builder.createOrFold<comb::OrOp>(loc, ValueRange{lhs, rhs}, true);
}

static FailureOr<unsigned> getBitWidth(Operation *op, Type type) {
  int64_t width = hw::getBitWidth(type);
  if (width < 0 || width > std::numeric_limits<unsigned>::max())
    return op->emitError() << "PDR requires statically known bit widths";
  return static_cast<unsigned>(width);
}

static FailureOr<std::optional<APInt>>
getInitialValue(verif::BoundedModelCheckingOp bmc, Attribute attr) {
  if (isa<UnitAttr>(attr))
    return std::optional<APInt>();
  auto integer = dyn_cast<IntegerAttr>(attr);
  if (!integer)
    return bmc.emitError()
           << "PDR currently supports only integer or unit initial values";
  return std::optional<APInt>(integer.getValue());
}

static FailureOr<Value> lookupLocal(Block &sourceBlock, IRMapping &mapping,
                                    Value value, Operation *user) {
  Value mapped = mapping.lookupOrDefault(value);
  if (mapped == value) {
    if (auto arg = dyn_cast<BlockArgument>(value)) {
      if (arg.getOwner() == &sourceBlock)
        return user->emitError() << "unmapped BMC circuit block argument";
    } else if (value.getDefiningOp() &&
               value.getDefiningOp()->getBlock() == &sourceBlock) {
      return user->emitError() << "unmapped BMC circuit value";
    }
  }
  return mapped;
}

static FailureOr<Value> getAssertLikeHolds(Block &sourceBlock,
                                           OpBuilder &builder,
                                           IRMapping &mapping, Operation *op,
                                           Value property, Value enable) {
  auto mappedProperty = lookupLocal(sourceBlock, mapping, property, op);
  if (failed(mappedProperty))
    return failure();
  if (!mappedProperty->getType().isInteger(1))
    return op->emitError() << "PDR only supports single-bit properties";

  if (!enable)
    return *mappedProperty;

  auto mappedEnable = lookupLocal(sourceBlock, mapping, enable, op);
  if (failed(mappedEnable))
    return failure();
  if (!mappedEnable->getType().isInteger(1))
    return op->emitError() << "PDR only supports single-bit property enables";

  return createOr(builder, op->getLoc(),
                  createNot(builder, op->getLoc(), *mappedEnable),
                  *mappedProperty);
}

static FailureOr<TransitionLoweringResult>
lowerBMCToTransitionModule(ModuleOp module, verif::BoundedModelCheckingOp bmc) {
  if (!bmc->use_empty())
    return bmc.emitError()
           << "PDR currently supports only resultless top-level verif.bmc ops";
  if (!bmc->hasAttr("fraig_lec.update_regs_every_step"))
    return bmc.emitError()
           << "PDR currently requires explicit every-step register updates";

  Block &circuitBlock = bmc.getCircuit().front();
  unsigned numRegs = bmc.getNumRegs();
  if (numRegs == 0)
    return bmc.emitError() << "PDR requires at least one BMC register";
  if (numRegs > circuitBlock.getNumArguments())
    return bmc.emitError() << "BMC num_regs exceeds circuit arguments";
  unsigned regStartIndex = circuitBlock.getNumArguments() - numRegs;

  auto yield = dyn_cast<verif::YieldOp>(circuitBlock.getTerminator());
  if (!yield)
    return circuitBlock.getTerminator()->emitError()
           << "expected verif.yield terminator in BMC circuit";
  if (yield.getInputs().size() < numRegs)
    return bmc.emitError() << "BMC circuit yields fewer values than num_regs";

  if (bmc.getInitialValues().size() != numRegs)
    return bmc.emitError() << "BMC initial value count does not match num_regs";

  OpBuilder moduleBuilder(module.getBodyRegion());
  moduleBuilder.setInsertionPointToEnd(module.getBody());
  Location loc = bmc.getLoc();
  SmallVector<std::string> stateNames;
  if (auto names = bmc->getAttrOfType<ArrayAttr>("fraig_lec.state_names")) {
    if (names.size() != numRegs)
      return bmc.emitError() << "BMC state name count does not match num_regs";
    for (auto [index, attr] : llvm::enumerate(names)) {
      auto name = dyn_cast<StringAttr>(attr);
      if (!name)
        return bmc.emitError()
               << "BMC state name #" << index << " is not a string attribute";
      stateNames.push_back(name.getValue().str());
    }
  } else {
    for (unsigned regIndex = 0; regIndex != numRegs; ++regIndex)
      stateNames.push_back(("state_reg" + Twine(regIndex)).str());
  }

  SmallVector<std::string> inputNames;
  bool hasInputNames = false;
  if (auto names = bmc->getAttrOfType<ArrayAttr>("fraig_lec.input_names")) {
    hasInputNames = true;
    for (auto [index, attr] : llvm::enumerate(names)) {
      auto name = dyn_cast<StringAttr>(attr);
      if (!name)
        return bmc.emitError()
               << "BMC input name #" << index << " is not a string attribute";
      inputNames.push_back(name.getValue().str());
    }
  }

  SmallVector<hw::PortInfo> ports;
  unsigned inputIndex = 0;
  unsigned primaryInputIndex = 0;
  SmallVector<std::optional<APInt>> initialValues;
  initialValues.reserve(numRegs);
  for (auto [argIndex, arg] : llvm::enumerate(circuitBlock.getArguments())) {
    if (isa<seq::ClockType>(arg.getType())) {
      if (!arg.use_empty())
        return bmc.emitError()
               << "PDR cannot normalize clock-dependent BMC circuit logic yet";
      continue;
    }

    hw::PortInfo port;
    port.type = arg.getType();
    port.dir = hw::ModulePort::Direction::Input;
    port.argNum = inputIndex++;
    if (argIndex >= regStartIndex) {
      unsigned regIndex = argIndex - regStartIndex;
      port.name = moduleBuilder.getStringAttr("state_reg" + Twine(regIndex));
      auto initial = getInitialValue(bmc, bmc.getInitialValues()[regIndex]);
      if (failed(initial))
        return failure();
      initialValues.push_back(*initial);
    } else {
      port.name = moduleBuilder.getStringAttr("input" + Twine(argIndex));
      if (!hasInputNames)
        inputNames.push_back(("input" + Twine(argIndex)).str());
      else if (primaryInputIndex >= inputNames.size())
        return bmc.emitError()
               << "BMC input name count does not match circuit inputs";
      ++primaryInputIndex;
    }
    ports.push_back(port);
  }
  if (primaryInputIndex != inputNames.size())
    return bmc.emitError()
           << "BMC input name count does not match circuit inputs";

  unsigned outputIndex = 0;
  hw::PortInfo initialPort;
  initialPort.name = moduleBuilder.getStringAttr("initial");
  initialPort.type = moduleBuilder.getI1Type();
  initialPort.dir = hw::ModulePort::Direction::Output;
  initialPort.argNum = outputIndex++;
  ports.push_back(initialPort);

  hw::PortInfo constraintPort;
  constraintPort.name = moduleBuilder.getStringAttr("constraint");
  constraintPort.type = moduleBuilder.getI1Type();
  constraintPort.dir = hw::ModulePort::Direction::Output;
  constraintPort.argNum = outputIndex++;
  ports.push_back(constraintPort);

  hw::PortInfo badPort;
  badPort.name = moduleBuilder.getStringAttr("bad");
  badPort.type = moduleBuilder.getI1Type();
  badPort.dir = hw::ModulePort::Direction::Output;
  badPort.argNum = outputIndex++;
  ports.push_back(badPort);

  for (unsigned regIndex = 0; regIndex != numRegs; ++regIndex) {
    Value next =
        yield.getInputs()[yield.getInputs().size() - numRegs + regIndex];
    hw::PortInfo port;
    port.name = moduleBuilder.getStringAttr("next_reg" + Twine(regIndex));
    port.type = next.getType();
    port.dir = hw::ModulePort::Direction::Output;
    port.argNum = outputIndex++;
    ports.push_back(port);
  }

  auto transition = hw::HWModuleOp::create(
      moduleBuilder, loc, moduleBuilder.getStringAttr("__pdr_transition"),
      ports);
  Block *body = transition.getBodyBlock();
  if (!body->empty())
    body->back().erase();
  OpBuilder bodyBuilder = OpBuilder::atBlockEnd(body);

  IRMapping mapping;
  unsigned nextInput = 0;
  for (auto [argIndex, arg] : llvm::enumerate(circuitBlock.getArguments())) {
    if (isa<seq::ClockType>(arg.getType()))
      continue;
    mapping.map(arg, body->getArgument(nextInput++));
    (void)argIndex;
  }

  SmallVector<Value> assumptions;
  SmallVector<Value> initialAssumptions;
  SmallVector<Value> failures;
  for (Operation &op : circuitBlock.without_terminator()) {
    if (auto assertOp = dyn_cast<verif::AssertOp>(op)) {
      auto holds =
          getAssertLikeHolds(circuitBlock, bodyBuilder, mapping, &op,
                             assertOp.getProperty(), assertOp.getEnable());
      if (failed(holds))
        return failure();
      failures.push_back(createNot(bodyBuilder, op.getLoc(), *holds));
      continue;
    }
    if (auto assumeOp = dyn_cast<verif::AssumeOp>(op)) {
      auto holds =
          getAssertLikeHolds(circuitBlock, bodyBuilder, mapping, &op,
                             assumeOp.getProperty(), assumeOp.getEnable());
      if (failed(holds))
        return failure();
      if (assumeOp->hasAttr(kInitialOnlyAttr))
        initialAssumptions.push_back(*holds);
      else
        assumptions.push_back(*holds);
      continue;
    }
    if (isa<verif::CoverOp, verif::ClockedAssertOp, verif::ClockedAssumeOp,
            verif::ClockedCoverOp>(op))
      return op.emitError() << "PDR only supports unclocked assertions and "
                               "assumptions in BMC circuit logic";
    if (isa<seq::ToClockOp, seq::FromClockOp>(op))
      return op.emitError()
             << "PDR cannot normalize clock-dependent BMC circuit logic yet";

    Operation *cloned = bodyBuilder.clone(op, mapping);
    for (auto [sourceResult, clonedResult] :
         llvm::zip(op.getResults(), cloned->getResults()))
      mapping.map(sourceResult, clonedResult);
  }

  Value guard = getTrue(bodyBuilder, loc);
  for (Value assumption : assumptions)
    guard = createAnd(bodyBuilder, loc, guard, assumption);

  Value initial = getTrue(bodyBuilder, loc);
  for (Value assumption : initialAssumptions)
    initial = createAnd(bodyBuilder, loc, initial, assumption);

  Value bad = getFalse(bodyBuilder, loc);
  for (Value failure : failures)
    bad = createOr(bodyBuilder, loc, bad, failure);

  SmallVector<Value> outputs;
  outputs.push_back(initial);
  outputs.push_back(guard);
  outputs.push_back(bad);
  for (Value next : yield.getInputs().take_back(numRegs)) {
    auto mapped = lookupLocal(circuitBlock, mapping, next, yield);
    if (failed(mapped))
      return failure();
    outputs.push_back(*mapped);
  }
  hw::OutputOp::create(bodyBuilder, loc, outputs);

  return TransitionLoweringResult{transition, std::move(initialValues),
                                  std::move(stateNames), std::move(inputNames)};
}

static constexpr int64_t kPDRMaxDivModEmulationUnknownBits = 16;

static LogicalResult rewritePDRArrayInjects(ModuleOp module) {
  SmallVector<hw::ArrayInjectOp> injects;
  module.walk([&](hw::ArrayInjectOp op) { injects.push_back(op); });
  LDBG() << "pdr: rewriting array_inject ops=" << injects.size() << "\n";

  for (auto inject : injects) {
    auto arrayType =
        cast<hw::ArrayType>(hw::getCanonicalType(inject.getInput().getType()));
    size_t numElements = arrayType.getNumElements();
    Location loc = inject.getLoc();
    OpBuilder builder(inject);

    SmallVector<Value> elements;
    elements.reserve(numElements);
    if (numElements == 1) {
      elements.push_back(inject.getElement());
    } else {
      unsigned indexWidth = inject.getIndex().getType().getIntOrFloatBitWidth();
      for (size_t i = 0; i != numElements; ++i) {
        auto indexConstant =
            hw::ConstantOp::create(builder, loc, APInt(indexWidth, i));
        Value oldElement = hw::ArrayGetOp::create(
            builder, loc, inject.getInput(), indexConstant);
        Value selected = builder.createOrFold<comb::ICmpOp>(
            loc, comb::ICmpPredicate::eq, inject.getIndex(), indexConstant);
        elements.push_back(builder.createOrFold<comb::MuxOp>(
            loc, selected, inject.getElement(), oldElement));
      }
    }

    SmallVector<Value> arrayCreateInputs;
    arrayCreateInputs.reserve(elements.size());
    for (Value element : llvm::reverse(elements))
      arrayCreateInputs.push_back(element);

    Value replacement =
        hw::ArrayCreateOp::create(builder, loc, arrayCreateInputs);
    inject.replaceAllUsesWith(replacement);
    inject.erase();
  }

  return success();
}

static void rewriteTwoBitMuls(ModuleOp module) {
  SmallVector<comb::MulOp> muls;
  module.walk([&](comb::MulOp mul) {
    if (mul.getInputs().size() == 2 &&
        mul.getType().getIntOrFloatBitWidth() == 2)
      muls.push_back(mul);
  });
  if (!muls.empty())
    LDBG() << "pdr: rewrite two-bit muls count=" << muls.size() << "\n";

  for (comb::MulOp mul : muls) {
    OpBuilder builder(mul);
    Location loc = mul.getLoc();
    Value lhs = mul.getInputs()[0];
    Value rhs = mul.getInputs()[1];
    Value lhs0 = builder.createOrFold<comb::ExtractOp>(loc, lhs, 0, 1);
    Value lhs1 = builder.createOrFold<comb::ExtractOp>(loc, lhs, 1, 1);
    Value rhs0 = builder.createOrFold<comb::ExtractOp>(loc, rhs, 0, 1);
    Value rhs1 = builder.createOrFold<comb::ExtractOp>(loc, rhs, 1, 1);

    Value lo = builder.createOrFold<comb::AndOp>(loc, lhs0, rhs0);
    Value hi0 = builder.createOrFold<comb::AndOp>(loc, lhs1, rhs0);
    Value hi1 = builder.createOrFold<comb::AndOp>(loc, lhs0, rhs1);
    Value hi = builder.createOrFold<comb::XorOp>(loc, hi0, hi1);
    Value product = comb::ConcatOp::create(builder, loc, ValueRange{hi, lo});
    mul.replaceAllUsesWith(product);
    mul.erase();
  }
}

static LogicalResult normalizeTransitionModule(ModuleOp module,
                                               unsigned maxDivModUnknownBits) {
  LDBG() << "pdr: normalize transition begin ops=" << countOperations(module)
         << " divmod-limit=" << maxDivModUnknownBits << "\n";

  if (failed(rewritePDRArrayInjects(module)))
    return failure();
  rewriteTwoBitMuls(module);

  PassManager pm(module.getContext());
  if (failed(applyPassManagerCLOptions(pm)))
    return failure();

  pm.addNestedPass<hw::HWModuleOp>(hw::createHWAggregateToComb());
  pm.addPass(createCSEPass());
  pm.addPass(createSimpleCanonicalizerPass());
  pm.addPass(createCSEPass());
  pm.addNestedPass<hw::HWModuleOp>(hw::createHWAggregateToComb());

  ConvertCombToSynthOptions options;
  options.forceAIG = true;
  options.maxEmulationUnknownBits = maxDivModUnknownBits;
  pm.addPass(createConvertCombToSynth(options));
  pm.addPass(synth::createLowerWordToBits());
  pm.addPass(createConvertCombToSynth(options));
  pm.addPass(createCSEPass());
  pm.addPass(createSimpleCanonicalizerPass());
  pm.addPass(createCSEPass());
  if (failed(pm.run(module)))
    return failure();

  LDBG() << "pdr: normalize transition end ops=" << countOperations(module)
         << "\n";
  return success();
}

static bool isHWAggregateType(Type type) {
  type = hw::getCanonicalType(type);
  return isa<hw::ArrayType, hw::StructType, hw::UnpackedArrayType>(type);
}

static LogicalResult checkPDRSupportedAggregateWidths(hw::HWModuleOp module) {
  constexpr int64_t maxScalarBitWidth = (1 << 24) - 1;

  auto checkType = [&](Operation *op, Type type) -> LogicalResult {
    if (!isHWAggregateType(type))
      return success();
    int64_t width = hw::getBitWidth(type);
    if (width < 0)
      return op->emitError()
             << "PDR aggregate lowering cannot determine a supported scalar "
                "bit width for type "
             << type
             << "; large arrays are not supported by this prototype "
                "PDR flow";
    if (width <= maxScalarBitWidth)
      return success();
    return op->emitError()
           << "PDR aggregate lowering would create a " << width
           << "-bit scalar, exceeding the MLIR integer width limit; large "
              "arrays are not supported by this prototype PDR flow";
  };

  for (BlockArgument arg : module.getBodyBlock()->getArguments())
    if (failed(checkType(module, arg.getType())))
      return failure();

  WalkResult walk = module.walk([&](Operation *op) {
    for (Type type : op->getResultTypes())
      if (failed(checkType(op, type)))
        return WalkResult::interrupt();
    if (auto inject = dyn_cast<hw::ArrayInjectOp>(op)) {
      auto arrayType = cast<hw::ArrayType>(
          hw::getCanonicalType(inject.getInput().getType()));
      int64_t arrayWidth = hw::getBitWidth(arrayType);
      size_t numElements = arrayType.getNumElements();
      if (arrayWidth < 0 ||
          (arrayWidth != 0 &&
           numElements > static_cast<size_t>(maxScalarBitWidth / arrayWidth))) {
        op->emitError()
            << "PDR aggregate lowering for 'hw.array_inject' would create "
               "an intermediate wider than "
            << maxScalarBitWidth
            << " bits; large arrays are not supported by this prototype PDR "
               "flow";
        return WalkResult::interrupt();
      }
    }
    return WalkResult::advance();
  });
  return success(!walk.wasInterrupted());
}

static int64_t getPDRDivModUnknownBits(Value value) {
  if (value.getType().isInteger(0))
    return 0;

  if (auto concat = value.getDefiningOp<comb::ConcatOp>()) {
    int64_t total = 0;
    for (Value input : concat.getInputs()) {
      int64_t unknownBits = getPDRDivModUnknownBits(input);
      if (unknownBits < 0)
        return unknownBits;
      total += unknownBits;
    }
    return total;
  }

  if (value.getDefiningOp<hw::ConstantOp>())
    return 0;

  return hw::getBitWidth(value.getType());
}

static LogicalResult checkPDRSupportedCombOps(hw::HWModuleOp module,
                                              unsigned maxDivModUnknownBits) {
  WalkResult walk = module.walk([&](Operation *op) {
    if (!isa<comb::DivUOp, comb::DivSOp, comb::ModUOp, comb::ModSOp>(op))
      return WalkResult::advance();

    int64_t unknownBits = 0;
    for (Value operand : op->getOperands()) {
      int64_t operandUnknownBits = getPDRDivModUnknownBits(operand);
      if (operandUnknownBits < 0) {
        unknownBits = operandUnknownBits;
        break;
      }
      unknownBits += operandUnknownBits;
    }
    if (unknownBits >= 0 && unknownBits <= maxDivModUnknownBits)
      return WalkResult::advance();

    op->emitError() << "PDR transition normalization can only emulate '"
                    << op->getName() << "' with at most "
                    << maxDivModUnknownBits
                    << " unknown operand bits; this operation requires "
                    << unknownBits;
    return WalkResult::interrupt();
  });
  return success(!walk.wasInterrupted());
}

static bool startsWith(StringAttr attr, StringRef prefix) {
  return attr && attr.getValue().starts_with(prefix);
}

static FailureOr<TransitionSystem> collectTransitionSystem(
    hw::HWModuleOp module, ArrayRef<std::optional<APInt>> initialValues,
    ArrayRef<std::string> stateNames, ArrayRef<std::string> inputNames) {
  auto output = dyn_cast<hw::OutputOp>(module.getBodyBlock()->getTerminator());
  if (!output || output.getOutputs().empty())
    return module.emitError() << "PDR transition module must have outputs";

  SmallVector<BlockArgument> stateInputs;
  SmallVector<Value> nextOutputs;
  TransitionSystem system;
  Value initial;
  Value constraint;
  Value bad;
  unsigned primaryInputIndex = 0;
  for (auto port : module.getPortList()) {
    if (port.isInput() && startsWith(port.name, "state_reg"))
      stateInputs.push_back(module.getBodyBlock()->getArgument(port.argNum));
    if (port.isInput() && !startsWith(port.name, "state_reg")) {
      BlockArgument arg = module.getBodyBlock()->getArgument(port.argNum);
      auto width = getBitWidth(module, arg.getType());
      if (failed(width))
        return failure();
      InputInfo input;
      input.input = arg;
      if (primaryInputIndex < inputNames.size())
        input.name = inputNames[primaryInputIndex];
      else
        input.name = port.name.getValue().str();
      input.width = *width;
      system.inputs.push_back(std::move(input));
      ++primaryInputIndex;
    }
    if (port.isOutput() && port.name.getValue() == "initial")
      initial = output.getOutputs()[port.argNum];
    if (port.isOutput() && port.name.getValue() == "constraint")
      constraint = output.getOutputs()[port.argNum];
    if (port.isOutput() && port.name.getValue() == "bad")
      bad = output.getOutputs()[port.argNum];
    if (port.isOutput() && startsWith(port.name, "next_reg"))
      nextOutputs.push_back(output.getOutputs()[port.argNum]);
  }

  if (!initial)
    return module.emitError()
           << "PDR transition module is missing initial output";
  if (!constraint)
    return module.emitError()
           << "PDR transition module is missing constraint output";
  if (!bad)
    return module.emitError() << "PDR transition module is missing bad output";
  if (stateInputs.empty())
    return module.emitError() << "PDR transition module has no state inputs";
  if (stateInputs.size() != nextOutputs.size())
    return module.emitError()
           << "PDR transition module state/next output count mismatch";
  if (initialValues.size() != stateInputs.size())
    return module.emitError()
           << "PDR transition initial value count mismatch after lowering";
  if (stateNames.size() != stateInputs.size())
    return module.emitError()
           << "PDR transition state name count mismatch after lowering";
  if (inputNames.size() != system.inputs.size())
    return module.emitError()
           << "PDR transition input name count mismatch after lowering";

  system.module = module;
  system.initial = initial;
  system.constraint = constraint;
  system.bad = bad;
  for (auto [index, input] : llvm::enumerate(stateInputs)) {
    auto width = getBitWidth(module, input.getType());
    if (failed(width))
      return failure();
    auto nextWidth = getBitWidth(module, nextOutputs[index].getType());
    if (failed(nextWidth))
      return failure();
    if (*width != *nextWidth)
      return module.emitError()
             << "PDR transition state/next bit width mismatch";
    StateInfo state;
    state.input = input;
    state.next = nextOutputs[index];
    state.name = stateNames[index];
    state.initialValue = initialValues[index];
    state.width = *width;
    state.offset = system.numStateBits;
    system.numStateBits += *width;
    system.states.push_back(std::move(state));
  }
  return system;
}

FailureOr<unsigned> QueryEncoder::getKnownBitWidth(Type type) {
  int64_t width = hw::getBitWidth(type);
  if (width < 0 || width > std::numeric_limits<unsigned>::max())
    return system.module.emitError()
           << "PDR requires statically known bit widths after normalization";
  return static_cast<unsigned>(width);
}

void QueryEncoder::setLitDeps(int lit, ArrayRef<unsigned> deps) {
  SmallVector<unsigned> sortedDeps(deps.begin(), deps.end());
  llvm::sort(sortedDeps);
  sortedDeps.erase(std::unique(sortedDeps.begin(), sortedDeps.end()),
                   sortedDeps.end());
  literalStateDeps[std::abs(lit)] = std::move(sortedDeps);
}

SmallVector<unsigned> QueryEncoder::getLitDeps(int lit) const {
  auto it = literalStateDeps.find(std::abs(lit));
  if (it == literalStateDeps.end())
    return {};
  return it->second;
}

SmallVector<unsigned> QueryEncoder::unionLitDeps(ArrayRef<int> lits) const {
  SmallVector<unsigned> deps;
  for (int lit : lits) {
    auto litDeps = getLitDeps(lit);
    deps.append(litDeps.begin(), litDeps.end());
  }
  llvm::sort(deps);
  deps.erase(std::unique(deps.begin(), deps.end()), deps.end());
  return deps;
}

void QueryEncoder::markLitDeps(int lit) {
  auto deps = getLitDeps(lit);
  for (unsigned bit : deps)
    usedCurrentStateBits.insert(bit);
}

FailureOr<SmallVector<int>>
QueryEncoder::getBlockArgumentBits(BlockArgument arg) {
  if (const StateInfo *state = getStateInfo(arg)) {
    SmallVector<int> bits;
    bits.reserve(state->width);
    for (unsigned bit = 0; bit != state->width; ++bit) {
      usedCurrentStateBits.insert(state->offset + bit);
      bits.push_back(currentStateVars[state->offset + bit]);
    }
    return bits;
  }

  auto width = getKnownBitWidth(arg.getType());
  if (failed(width))
    return failure();
  auto &bits = inputBits[arg];
  if (bits.empty()) {
    bits.reserve(*width);
    for (unsigned bit = 0; bit != *width; ++bit) {
      int var = solver.newVar();
      setLitDeps(var, {});
      bits.push_back(var);
    }
  }
  return bits;
}

FailureOr<SmallVector<int>>
QueryEncoder::getConstantBits(Value value, const APInt &constant) {
  auto width = getKnownBitWidth(value.getType());
  if (failed(width))
    return failure();
  SmallVector<int> bits;
  bits.reserve(*width);
  APInt adjusted = constant.zextOrTrunc(*width);
  for (unsigned bit = 0; bit != *width; ++bit) {
    int var = solver.newVar();
    solver.addClause({adjusted[bit] ? var : -var});
    setLitDeps(var, {});
    bits.push_back(var);
  }
  return bits;
}

FailureOr<SmallVector<int>> QueryEncoder::getConcatBits(comb::ConcatOp concat) {
  SmallVector<int> bits;
  for (Value input : llvm::reverse(concat.getInputs())) {
    auto inputBits = getBits(input);
    if (failed(inputBits))
      return failure();
    bits.append(inputBits->begin(), inputBits->end());
  }
  return bits;
}

FailureOr<SmallVector<int>>
QueryEncoder::getExtractBits(comb::ExtractOp extract) {
  if (auto arg = dyn_cast<BlockArgument>(extract.getInput())) {
    if (getStateInfo(arg)) {
      SmallVector<int> bits;
      unsigned low = extract.getLowBit();
      unsigned width = extract.getResult().getType().getIntOrFloatBitWidth();
      bits.reserve(width);
      for (unsigned bit = low, end = low + width; bit != end; ++bit) {
        auto stateBit = getStateBit(arg, bit);
        if (failed(stateBit))
          return failure();
        bits.push_back(*stateBit);
      }
      return bits;
    }
  }

  auto inputBits = getBits(extract.getInput());
  if (failed(inputBits))
    return failure();
  SmallVector<int> bits;
  unsigned low = extract.getLowBit();
  unsigned width = extract.getResult().getType().getIntOrFloatBitWidth();
  if (low + width > inputBits->size())
    return extract.emitError() << "PDR extract is out of range";
  bits.append(inputBits->begin() + low, inputBits->begin() + low + width);
  return bits;
}

FailureOr<SmallVector<int>>
QueryEncoder::getReplicateBits(comb::ReplicateOp replicate) {
  auto inputBits = getBits(replicate.getInput());
  if (failed(inputBits))
    return failure();

  SmallVector<int> bits;
  bits.reserve(inputBits->size() * replicate.getMultiple());
  for (uint32_t i = 0, e = replicate.getMultiple(); i != e; ++i)
    bits.append(inputBits->begin(), inputBits->end());
  return bits;
}

FailureOr<SmallVector<int>>
QueryEncoder::getBitcastBits(hw::BitcastOp bitcast) {
  auto inputBits = getBits(bitcast.getInput());
  if (failed(inputBits))
    return failure();
  auto resultWidth = getKnownBitWidth(bitcast.getResult().getType());
  if (failed(resultWidth))
    return failure();
  if (*resultWidth != inputBits->size())
    return bitcast.emitError() << "PDR bitcast changed bit width";
  return *inputBits;
}

FailureOr<SmallVector<int>>
QueryEncoder::getBooleanLogicBits(synth::BooleanLogicOpInterface op) {
  if (!op->getResult(0).getType().isInteger(1))
    return op->emitError()
           << "PDR expects bit-blasted single-bit synthesis logic";
  int out = solver.newVar();
  SmallVector<int> inputs;
  inputs.reserve(op->getNumOperands());
  for (Value input : op.getInputs()) {
    auto bit = getBit(input, 0);
    if (failed(bit))
      return failure();
    inputs.push_back(*bit);
  }
  op.emitCNF(
      out, inputs, [&](ArrayRef<int> clause) { solver.addClause(clause); },
      [&]() { return solver.newVar(); });
  setLitDeps(out, unionLitDeps(inputs));
  return SmallVector<int>{out};
}

FailureOr<SmallVector<int>> QueryEncoder::getBits(Value value) {
  if (auto it = encodedBits.find(value); it != encodedBits.end())
    return it->second;

  if (!encodingValues.insert(value).second)
    return system.module.emitError() << "cycle in PDR SAT encoding";
  llvm::scope_exit cleanup([&] { encodingValues.erase(value); });

  FailureOr<SmallVector<int>> result = failure();
  if (auto arg = dyn_cast<BlockArgument>(value)) {
    result = getBlockArgumentBits(arg);
  } else {
    APInt constant;
    Operation *op = value.getDefiningOp();
    if (matchPattern(value, m_ConstantInt(&constant))) {
      result = getConstantBits(value, constant);
    } else if (auto logic =
                   dyn_cast_or_null<synth::BooleanLogicOpInterface>(op)) {
      result = getBooleanLogicBits(logic);
    } else if (auto extract = dyn_cast_or_null<comb::ExtractOp>(op)) {
      result = getExtractBits(extract);
    } else if (auto concat = dyn_cast_or_null<comb::ConcatOp>(op)) {
      result = getConcatBits(concat);
    } else if (auto bitcast = dyn_cast_or_null<hw::BitcastOp>(op)) {
      result = getBitcastBits(bitcast);
    } else if (auto replicate = dyn_cast_or_null<comb::ReplicateOp>(op)) {
      result = getReplicateBits(replicate);
    } else {
      return op ? op->emitError()
                      << "unsupported operation in normalized PDR netlist: "
                      << op->getName()
                : system.module.emitError() << "PDR expected a defining op";
    }
  }

  if (failed(result))
    return failure();
  encodedBits[value] = *result;
  return *result;
}

FailureOr<int> QueryEncoder::getBit(Value value, unsigned bit) {
  if (auto arg = dyn_cast<BlockArgument>(value)) {
    if (getStateInfo(arg))
      return getStateBit(arg, bit);
  }

  auto bits = getBits(value);
  if (failed(bits))
    return failure();
  if (bit >= bits->size())
    return system.module.emitError() << "PDR bit access is out of range";
  int lit = (*bits)[bit];
  markLitDeps(lit);
  return lit;
}

const StateInfo *QueryEncoder::getStateInfo(BlockArgument arg) const {
  for (const StateInfo &state : system.states)
    if (state.input == arg)
      return &state;
  return nullptr;
}

FailureOr<int> QueryEncoder::getStateBit(BlockArgument arg, unsigned bit) {
  const StateInfo *state = getStateInfo(arg);
  if (!state)
    return failure();
  if (bit >= state->width)
    return system.module.emitError() << "PDR state bit access is out of range";
  unsigned stateBit = state->offset + bit;
  usedCurrentStateBits.insert(stateBit);
  return currentStateVars[stateBit];
}

void QueryEncoder::addEquivalence(int lhs, int rhs) {
  solver.addClause({-lhs, rhs});
  solver.addClause({lhs, -rhs});
}

LogicalResult QueryEncoder::encodeTransitionForCube(ArrayRef<int> cube) {
  for (int lit : cube) {
    unsigned stateBit = std::abs(lit) - 1;
    if (!encodedNextStateBits.insert(stateBit).second) {
      markLitDeps(nextStateVars[stateBit]);
      continue;
    }
    const StateInfo *selectedState = nullptr;
    unsigned selectedBit = 0;
    for (const StateInfo &state : system.states) {
      if (stateBit < state.offset || stateBit >= state.offset + state.width)
        continue;
      selectedState = &state;
      selectedBit = stateBit - state.offset;
      break;
    }
    if (!selectedState)
      return system.module.emitError()
             << "PDR transition cube references an unknown state bit";
    auto next = getBit(selectedState->next, selectedBit);
    if (failed(next))
      return failure();
    setLitDeps(nextStateVars[stateBit], getLitDeps(*next));
    addEquivalence(nextStateVars[stateBit], *next);
  }
  return success();
}

LogicalResult QueryEncoder::encodeInitial() {
  auto initial = getBit(system.initial, 0);
  if (failed(initial))
    return failure();
  initialLit = *initial;
  return success();
}

LogicalResult QueryEncoder::encodeConstraint() {
  auto constraint = getBit(system.constraint, 0);
  if (failed(constraint))
    return failure();
  constraintLit = *constraint;
  return success();
}

LogicalResult QueryEncoder::encodeBad() {
  auto bad = getBit(system.bad, 0);
  if (failed(bad))
    return failure();
  badLit = *bad;
  return success();
}

int QueryEncoder::mapStateLit(int lit, bool next) const {
  unsigned index = std::abs(lit) - 1;
  int var = next ? nextStateVars[index] : currentStateVars[index];
  return lit > 0 ? var : -var;
}

void QueryEncoder::addFrameClause(ArrayRef<int> clause) {
  SmallVector<int> mapped;
  mapped.reserve(clause.size());
  for (int lit : clause) {
    permanentCurrentStateBits.insert(std::abs(lit) - 1);
    usedCurrentStateBits.insert(std::abs(lit) - 1);
    mapped.push_back(mapStateLit(lit, /*next=*/false));
  }
  solver.addClause(mapped);
}

void QueryEncoder::assumeStateCube(ArrayRef<int> cube, bool next) {
  for (int lit : cube) {
    if (!next)
      usedCurrentStateBits.insert(std::abs(lit) - 1);
    solver.assume(mapStateLit(lit, next));
  }
}

Cube QueryEncoder::getCurrentStateCube() const {
  Cube cube;
  SmallVector<unsigned> bits(usedCurrentStateBits.begin(),
                             usedCurrentStateBits.end());
  llvm::sort(bits);
  cube.reserve(bits.size());
  for (unsigned bit : bits) {
    int var = currentStateVars[bit];
    cube.push_back(solver.val(var) > 0 ? int(bit + 1) : -int(bit + 1));
  }
  return cube;
}

Cube QueryEncoder::getFullCurrentStateCube() const {
  Cube cube;
  cube.reserve(currentStateVars.size());
  for (auto [bit, var] : llvm::enumerate(currentStateVars))
    cube.push_back(solver.val(var) > 0 ? int(bit + 1) : -int(bit + 1));
  return cube;
}

Cube QueryEncoder::getFailedStateCube(ArrayRef<int> cube, bool next) const {
  // The current CIRCT SAT abstraction does not expose failed assumption cores.
  // Return an empty cube so callers fall back to explicit cube shrinking.
  return {};
}

SmallVector<InputAssignment> QueryEncoder::getInputAssignments() const {
  SmallVector<InputAssignment> assignments;
  for (const InputInfo &input : system.inputs) {
    auto it = inputBits.find(input.input);
    if (it == inputBits.end())
      continue;

    InputAssignment assignment;
    assignment.name = input.name;
    assignment.width = input.width;
    assignment.bits = it->second;
    assignments.push_back(std::move(assignment));
  }
  return assignments;
}

SmallVector<InputValue> QueryEncoder::getInputValues() const {
  SmallVector<InputValue> values;
  for (const InputAssignment &assignment : getInputAssignments()) {
    APInt value(assignment.width, 0);
    for (auto [bit, var] : llvm::enumerate(assignment.bits))
      if (solver.val(var) > 0)
        value.setBit(bit);

    values.push_back(InputValue{assignment.name, std::move(value)});
  }
  return values;
}

std::unique_ptr<IncrementalSATSolver> PDREngine::createSolver() {
  auto solver = createSATSolver(solverName);
  if (solver)
    solver->setConflictLimit(static_cast<int>(conflictLimit));
  return solver;
}

Clause PDREngine::negateCube(ArrayRef<int> cube) const {
  Clause clause;
  clause.reserve(cube.size());
  for (int lit : cube)
    clause.push_back(-lit);
  return clause;
}

void PDREngine::traceProgress(StringRef event, unsigned frame,
                              unsigned cubeSize) {
  if ((numBadQueries + numPredecessorQueries + numBlockedCubes) % 1000 != 0)
    return;
  LDBG() << "pdr: " << event << " frame=" << frame << " cube=" << cubeSize
         << " bad=" << numBadQueries << " pred=" << numPredecessorQueries
         << " blocked=" << numBlockedCubes << " core=" << numUsedCores
         << " empty-core=" << numEmptyCores
         << " weak-core=" << numInitialWeakCores << "\n";
}

void PDREngine::dumpFramesForTrace() const {
  for (auto [index, frame] : llvm::enumerate(frames)) {
    LDBG() << "pdr-frame " << index << " clauses=" << frame.size() << "\n";
    for (const Clause &clause : frame) {
      LDBG() << "  clause";
      for (int lit : clause)
        LDBG() << " " << lit;
      LDBG() << "\n";
    }
  }
}

FailureOr<FrameContext *> PDREngine::getFrameContext(unsigned frame) {
  while (frameContexts.size() <= frame)
    frameContexts.push_back(nullptr);
  if (!frameContexts[frame]) {
    auto solver = createSolver();
    if (!solver)
      return system.module.emitError() << "no SAT solver available for PDR";
    auto encoder = std::make_unique<QueryEncoder>(system, *solver);
    if (failed(encoder->encodeConstraint()) || failed(encoder->encodeBad()))
      return failure();
    if (frame == 0) {
      if (failed(encoder->encodeInitial()))
        return failure();
      encoder->addInitial();
    }
    auto context = std::make_unique<FrameContext>();
    context->solver = std::move(solver);
    context->encoder = std::move(encoder);
    frameContexts[frame] = std::move(context);
  }
  if (failed(syncFrameClauses(frame)))
    return failure();
  return frameContexts[frame].get();
}

FailureOr<FrameContext *> PDREngine::getInitialImageContext() {
  if (!initialImageContext) {
    auto solver = createSolver();
    if (!solver)
      return system.module.emitError() << "no SAT solver available for PDR";
    auto encoder = std::make_unique<QueryEncoder>(system, *solver);
    if (failed(encoder->encodeConstraint()) || failed(encoder->encodeInitial()))
      return failure();
    encoder->addInitial();
    for (const Clause &clause : originalInitialClauses)
      encoder->addFrameClause(clause);

    auto context = std::make_unique<FrameContext>();
    context->solver = std::move(solver);
    context->encoder = std::move(encoder);
    initialImageContext = std::move(context);
  }
  return initialImageContext.get();
}

FailureOr<InitialPredecessorContext *>
PDREngine::getInitialPredecessorContext() {
  if (!initialPredecessorContext) {
    auto solver = createSolver();
    if (!solver)
      return system.module.emitError() << "no SAT solver available for PDR";

    auto initialEncoder = std::make_unique<QueryEncoder>(system, *solver);
    if (failed(initialEncoder->encodeConstraint()) ||
        failed(initialEncoder->encodeInitial()))
      return failure();
    initialEncoder->addInitial();
    for (const Clause &clause : originalInitialClauses)
      initialEncoder->addFrameClause(clause);

    Cube fullImage;
    fullImage.reserve(system.numStateBits);
    for (unsigned bit = 0, end = system.numStateBits; bit != end; ++bit)
      fullImage.push_back(int(bit + 1));
    if (failed(initialEncoder->encodeTransitionForCube(fullImage)))
      return failure();

    auto stepEncoder = std::make_unique<QueryEncoder>(
        system, *solver, initialEncoder->getNextStateVars());
    if (failed(stepEncoder->encodeConstraint()))
      return failure();

    auto context = std::make_unique<InitialPredecessorContext>();
    context->solver = std::move(solver);
    context->initialEncoder = std::move(initialEncoder);
    context->stepEncoder = std::move(stepEncoder);
    initialPredecessorContext = std::move(context);
  }
  if (failed(syncInitialPredecessorClauses()))
    return failure();
  return initialPredecessorContext.get();
}

LogicalResult PDREngine::syncFrameClauses(unsigned frame) {
  FrameContext *context = frameContexts[frame].get();
  assert(context && "expected frame context");
  while (context->numFrameClauses < frames[frame].size())
    context->encoder->addFrameClause(frames[frame][context->numFrameClauses++]);
  return success();
}

LogicalResult PDREngine::syncInitialPredecessorClauses() {
  InitialPredecessorContext *context = initialPredecessorContext.get();
  assert(context && "expected initial predecessor context");
  while (context->numFrameClauses < frames[0].size())
    context->stepEncoder->addFrameClause(frames[0][context->numFrameClauses++]);
  return success();
}

FailureOr<QueryResult> PDREngine::queryBad(unsigned frame) {
  ++numBadQueries;
  traceProgress("bad", frame, 0);
  auto context = getFrameContext(frame);
  if (failed(context))
    return failure();
  (*context)->encoder->beginQuery();
  if (frame == 0)
    (*context)->encoder->assumeInitial();
  (*context)->encoder->assumeConstraint();
  (*context)->encoder->assumeBad();
  QueryResult result;
  result.result = (*context)->solver->solve({});
  if (result.result == IncrementalSATSolver::kSAT)
    result.stateCube = useInitialImage
                           ? (*context)->encoder->getFullCurrentStateCube()
                           : (*context)->encoder->getCurrentStateCube();
  return result;
}

FailureOr<IncrementalSATSolver::Result>
PDREngine::queryInitialCube(ArrayRef<int> cube) {
  auto result = queryInitialCubeWithCore(cube);
  if (failed(result))
    return failure();
  return result->result;
}

FailureOr<QueryResult> PDREngine::queryInitialCubeWithCore(ArrayRef<int> cube) {
  if (useInitialImage)
    return queryInitialImageCubeWithCore(cube);

  auto context = getFrameContext(/*frame=*/0);
  if (failed(context))
    return failure();
  (*context)->encoder->beginQuery();
  (*context)->encoder->assumeInitial();
  (*context)->encoder->assumeConstraint();
  (*context)->encoder->assumeStateCube(cube, /*next=*/false);
  QueryResult result;
  result.result = (*context)->solver->solve({});
  if (result.result == IncrementalSATSolver::kUNSAT)
    result.failedCube =
        (*context)->encoder->getFailedStateCube(cube, /*next=*/false);
  return result;
}

FailureOr<QueryResult>
PDREngine::queryInitialImageCubeWithCore(ArrayRef<int> cube) {
  auto context = getInitialImageContext();
  if (failed(context))
    return failure();
  (*context)->encoder->beginQuery();
  if (failed((*context)->encoder->encodeTransitionForCube(cube)))
    return failure();
  (*context)->encoder->assumeInitial();
  (*context)->encoder->assumeConstraint();
  (*context)->encoder->assumeStateCube(cube, /*next=*/true);
  QueryResult result;
  result.result = (*context)->solver->solve({});
  if (result.result == IncrementalSATSolver::kUNSAT)
    result.failedCube =
        (*context)->encoder->getFailedStateCube(cube, /*next=*/true);
  return result;
}

FailureOr<QueryResult>
PDREngine::queryInitialImagePredecessor(ArrayRef<int> cube) {
  auto context = getInitialPredecessorContext();
  if (failed(context))
    return failure();

  (*context)->initialEncoder->beginQuery();
  (*context)->stepEncoder->beginQuery();
  if (failed((*context)->stepEncoder->encodeTransitionForCube(cube)))
    return failure();
  (*context)->initialEncoder->assumeInitial();
  (*context)->initialEncoder->assumeConstraint();
  (*context)->stepEncoder->assumeConstraint();
  (*context)->stepEncoder->assumeStateCube(cube, /*next=*/true);

  QueryResult result;
  result.result = (*context)->solver->solve({});
  if (result.result == IncrementalSATSolver::kSAT) {
    result.stateCube = (*context)->stepEncoder->getFullCurrentStateCube();
    result.inputValues = (*context)->stepEncoder->getInputValues();
  }
  if (result.result == IncrementalSATSolver::kUNSAT)
    result.failedCube =
        (*context)->stepEncoder->getFailedStateCube(cube, /*next=*/true);
  return result;
}

FailureOr<QueryResult> PDREngine::queryPredecessor(unsigned frame,
                                                   ArrayRef<int> cube) {
  ++numPredecessorQueries;
  traceProgress("pred", frame, cube.size());
  if (useInitialImage && frame == 0)
    return queryInitialImagePredecessor(cube);

  auto context = getFrameContext(frame);
  if (failed(context))
    return failure();
  (*context)->encoder->beginQuery();
  if (failed((*context)->encoder->encodeTransitionForCube(cube)))
    return failure();
  if (frame == 0)
    (*context)->encoder->assumeInitial();
  (*context)->encoder->assumeConstraint();
  (*context)->encoder->assumeStateCube(cube, /*next=*/true);

  QueryResult result;
  result.result = (*context)->solver->solve({});
  if (result.result == IncrementalSATSolver::kSAT) {
    result.stateCube = useInitialImage
                           ? (*context)->encoder->getFullCurrentStateCube()
                           : (*context)->encoder->getCurrentStateCube();
    result.inputValues = (*context)->encoder->getInputValues();
  }
  if (result.result == IncrementalSATSolver::kUNSAT)
    result.failedCube =
        (*context)->encoder->getFailedStateCube(cube, /*next=*/true);
  return result;
}

FailureOr<SmallVector<Clause>>
PDREngine::computeInitialImageUnits(ArrayRef<Clause> initialClauses) {
  auto solver = createSolver();
  if (!solver)
    return system.module.emitError() << "no SAT solver available for PDR";

  QueryEncoder encoder(system, *solver);
  if (failed(encoder.encodeConstraint()) || failed(encoder.encodeInitial()))
    return failure();
  encoder.addInitial();
  for (const Clause &clause : initialClauses)
    encoder.addFrameClause(clause);

  SmallVector<Clause> units;
  for (unsigned bit = 0, end = system.numStateBits; bit != end; ++bit) {
    int lit = int(bit + 1);
    Cube positive{lit};
    if (failed(encoder.encodeTransitionForCube(positive)))
      return failure();

    encoder.assumeConstraint();
    encoder.assumeStateCube(positive, /*next=*/true);
    auto positiveResult = solver->solve({});

    Cube negative{-lit};
    encoder.assumeConstraint();
    encoder.assumeStateCube(negative, /*next=*/true);
    auto negativeResult = solver->solve({});

    if (positiveResult == IncrementalSATSolver::kUNKNOWN ||
        negativeResult == IncrementalSATSolver::kUNKNOWN)
      return system.module.emitError()
             << "PDR initial-image query returned unknown";
    if (positiveResult == IncrementalSATSolver::kUNSAT &&
        negativeResult == IncrementalSATSolver::kUNSAT) {
      initialImageEmpty = true;
      return units;
    }

    if (positiveResult == IncrementalSATSolver::kUNSAT)
      units.push_back(Clause{-lit});
    else if (negativeResult == IncrementalSATSolver::kUNSAT)
      units.push_back(Clause{lit});
  }

  return units;
}

FailureOr<SmallVector<Clause>>
PDREngine::computeInductiveUnitInvariants(ArrayRef<Clause> initialImageUnits) {
  SmallVector<Clause> candidates(initialImageUnits.begin(),
                                 initialImageUnits.end());

  bool changed = true;
  while (changed) {
    changed = false;

    auto solver = createSolver();
    if (!solver)
      return system.module.emitError() << "no SAT solver available for PDR";

    QueryEncoder encoder(system, *solver);
    if (failed(encoder.encodeConstraint()))
      return failure();
    for (const Clause &clause : candidates)
      encoder.addFrameClause(clause);

    SmallVector<Clause> preserved;
    preserved.reserve(candidates.size());
    for (const Clause &clause : candidates) {
      if (clause.size() != 1)
        continue;

      int blockedLit = -clause.front();
      Cube target{blockedLit};
      if (failed(encoder.encodeTransitionForCube(target)))
        return failure();
      encoder.assumeConstraint();
      encoder.assumeStateCube(target, /*next=*/true);
      auto result = solver->solve({});
      if (result == IncrementalSATSolver::kUNKNOWN)
        return system.module.emitError()
               << "PDR unit-invariant query returned unknown";
      if (result == IncrementalSATSolver::kUNSAT) {
        preserved.push_back(clause);
        continue;
      }
      changed = true;
    }

    candidates = std::move(preserved);
  }

  return candidates;
}

FailureOr<SmallVector<Clause>> PDREngine::computeConstraintUnitInvariants() {
  auto solver = createSolver();
  if (!solver)
    return system.module.emitError() << "no SAT solver available for PDR";

  QueryEncoder encoder(system, *solver);
  if (failed(encoder.encodeConstraint()))
    return failure();

  SmallVector<Clause> units;
  for (unsigned bit = 0, end = system.numStateBits; bit != end; ++bit) {
    int lit = int(bit + 1);

    encoder.beginQuery();
    encoder.assumeConstraint();
    encoder.assumeStateCube(Cube{lit}, /*next=*/false);
    auto positiveResult = solver->solve({});

    encoder.beginQuery();
    encoder.assumeConstraint();
    encoder.assumeStateCube(Cube{-lit}, /*next=*/false);
    auto negativeResult = solver->solve({});

    if (positiveResult == IncrementalSATSolver::kUNKNOWN ||
        negativeResult == IncrementalSATSolver::kUNKNOWN)
      return system.module.emitError()
             << "PDR constraint-unit query returned unknown";
    if (positiveResult == IncrementalSATSolver::kUNSAT &&
        negativeResult == IncrementalSATSolver::kUNSAT) {
      constraintsEmpty = true;
      return units;
    }

    if (positiveResult == IncrementalSATSolver::kUNSAT)
      units.push_back(Clause{-lit});
    else if (negativeResult == IncrementalSATSolver::kUNSAT)
      units.push_back(Clause{lit});
  }

  return units;
}

FailureOr<Cube> PDREngine::generalizeCubeWithCores(Cube cube, unsigned frame) {
  for (unsigned iteration = 0; iteration != 8; ++iteration) {
    bool changed = false;

    auto predecessor = queryPredecessor(frame - 1, cube);
    if (failed(predecessor))
      return failure();
    if (predecessor->result != IncrementalSATSolver::kUNSAT)
      return cube;
    if (!predecessor->failedCube.empty() &&
        predecessor->failedCube.size() < cube.size()) {
      auto coreInitiallyPossible = queryInitialCube(predecessor->failedCube);
      if (failed(coreInitiallyPossible))
        return failure();
      if (*coreInitiallyPossible == IncrementalSATSolver::kUNSAT) {
        cube = std::move(predecessor->failedCube);
        changed = true;
        continue;
      }
      if (*coreInitiallyPossible != IncrementalSATSolver::kSAT)
        return system.module.emitError()
               << "PDR initial-state query returned unknown";
    }

    auto initiallyPossible = queryInitialCubeWithCore(cube);
    if (failed(initiallyPossible))
      return failure();
    if (initiallyPossible->result != IncrementalSATSolver::kUNSAT)
      return cube;
    if (!initiallyPossible->failedCube.empty() &&
        initiallyPossible->failedCube.size() < cube.size()) {
      Cube candidate = std::move(initiallyPossible->failedCube);
      auto candidatePredecessor = queryPredecessor(frame - 1, candidate);
      if (failed(candidatePredecessor))
        return failure();
      if (candidatePredecessor->result == IncrementalSATSolver::kUNSAT) {
        cube = std::move(candidate);
        changed = true;
        continue;
      }
      if (candidatePredecessor->result != IncrementalSATSolver::kSAT)
        return system.module.emitError()
               << "PDR generalization query returned unknown";
    }

    if (!changed)
      break;
  }

  return cube;
}

FailureOr<Cube> PDREngine::generalizeCube(Cube cube, unsigned frame) {
  auto coreGeneralized = generalizeCubeWithCores(std::move(cube), frame);
  if (failed(coreGeneralized))
    return failure();
  cube = std::move(*coreGeneralized);

  for (unsigned index = 0; index < cube.size();) {
    Cube candidate;
    candidate.reserve(cube.size() - 1);
    for (auto [candidateIndex, lit] : llvm::enumerate(cube))
      if (candidateIndex != index)
        candidate.push_back(lit);

    auto initiallyPossible = queryInitialCube(candidate);
    if (failed(initiallyPossible))
      return failure();
    if (*initiallyPossible != IncrementalSATSolver::kUNSAT) {
      ++index;
      continue;
    }

    auto predecessor = queryPredecessor(frame - 1, candidate);
    if (failed(predecessor))
      return failure();
    if (predecessor->result == IncrementalSATSolver::kUNSAT) {
      if (!predecessor->failedCube.empty()) {
        auto coreInitiallyPossible = queryInitialCube(predecessor->failedCube);
        if (failed(coreInitiallyPossible))
          return failure();
        if (*coreInitiallyPossible == IncrementalSATSolver::kUNSAT) {
          cube = std::move(predecessor->failedCube);
          index = 0;
          continue;
        }
        if (*coreInitiallyPossible != IncrementalSATSolver::kSAT)
          return system.module.emitError()
                 << "PDR initial-state query returned unknown";
      }
      cube = std::move(candidate);
      index = 0;
      continue;
    }
    if (predecessor->result != IncrementalSATSolver::kSAT)
      return system.module.emitError()
             << "PDR generalization query returned unknown";
    ++index;
  }
  return cube;
}

FailureOr<std::optional<Clause>>
PDREngine::propagateClause(unsigned frame, const Clause &clause) {
  Cube blockedCube;
  blockedCube.reserve(clause.size());
  for (int lit : clause)
    blockedCube.push_back(-lit);

  auto predecessor = queryPredecessor(frame, blockedCube);
  if (failed(predecessor))
    return failure();
  if (predecessor->result == IncrementalSATSolver::kUNSAT) {
    if (!predecessor->failedCube.empty() &&
        predecessor->failedCube.size() < blockedCube.size())
      return std::optional<Clause>(negateCube(predecessor->failedCube));
    return std::optional<Clause>(clause);
  }
  if (predecessor->result == IncrementalSATSolver::kSAT)
    return std::optional<Clause>();
  return system.module.emitError() << "PDR propagation query returned unknown";
}

LogicalResult PDREngine::propagateClauses(unsigned maxFrame) {
  for (unsigned frame = 1; frame <= maxFrame; ++frame) {
    SmallVector<Clause> clauses(frames[frame].begin(), frames[frame].end());
    for (const Clause &clause : clauses) {
      if (containsSubsumingClause(frame + 1, clause))
        continue;
      auto propagated = propagateClause(frame, clause);
      if (failed(propagated))
        return failure();
      if (*propagated)
        addFrameClause(frame + 1, std::move(**propagated));
    }
  }
  return success();
}

void PDREngine::addBlockedCube(ArrayRef<int> cube, unsigned frame) {
  ++numBlockedCubes;
  traceProgress("block", frame, cube.size());
  Clause clause = negateCube(cube);
  for (unsigned i = 1; i <= frame; ++i)
    addFrameClause(i, clause);
}

bool PDREngine::subsumes(const Clause &lhs, const Clause &rhs) const {
  if (lhs.size() > rhs.size())
    return false;
  for (int lit : lhs)
    if (!llvm::is_contained(rhs, lit))
      return false;
  return true;
}

void PDREngine::addFrameClause(unsigned frame, Clause clause) {
  for (const Clause &existing : frames[frame])
    if (subsumes(existing, clause))
      return;

  frames[frame].push_back(std::move(clause));
}

std::optional<APInt>
PDREngine::getStateValueFromCube(const StateInfo &state,
                                 ArrayRef<int> cube) const {
  APInt value(state.width, 0);
  for (unsigned bit = 0; bit != state.width; ++bit) {
    unsigned stateBit = state.offset + bit;
    auto it = llvm::find_if(
        cube, [&](int lit) { return unsigned(std::abs(lit) - 1) == stateBit; });
    if (it == cube.end())
      return std::nullopt;
    if (*it > 0)
      value.setBit(bit);
  }
  return value;
}

void PDREngine::printStateValue(raw_ostream &os, const StateInfo &state,
                                const APInt &value) const {
  printNamedStateValue(os, state.name, state.width, value);
}

void PDREngine::recordCounterexampleBase(ArrayRef<int> cube) {
  counterexampleStates.clear();
  counterexampleInputs.clear();
  counterexampleStates.push_back(Cube(cube));
  foundCounterexample = true;
}

void PDREngine::appendCounterexampleState(ArrayRef<int> cube,
                                          ArrayRef<InputValue> inputValues) {
  counterexampleInputs.push_back(SmallVector<InputValue>(inputValues));
  counterexampleStates.push_back(Cube(cube));
}

static void printInputValues(raw_ostream &os, unsigned frame,
                             ArrayRef<InputValue> inputs) {
  if (inputs.empty())
    return;

  os << "pdr:   input " << llvm::utostr(frame) << ":";
  for (const InputValue &input : inputs) {
    os << " ";
    printNamedStateValue(os, input.name, input.value.getBitWidth(),
                         input.value);
  }
  os << "\n";
}

void PDREngine::printCounterexampleTrace() const {
  if (counterexampleStates.empty())
    return;

  os << "pdr: counterexample trace:\n";
  for (auto [index, cube] : llvm::enumerate(counterexampleStates)) {
    os << "pdr:   state " << llvm::utostr(index) << ":";
    for (const StateInfo &state : system.states) {
      os << " ";
      auto value = getStateValueFromCube(state, cube);
      if (value) {
        printStateValue(os, state, *value);
        continue;
      }
      os << state.name << "=<partial>";
    }
    os << "\n";
    if (index < counterexampleInputs.size())
      printInputValues(os, index, counterexampleInputs[index]);
  }
}

LogicalResult PDREngine::blockCube(Cube cube, unsigned frame) {
  if (frame == 0) {
    auto initiallyPossible = queryInitialCube(cube);
    if (failed(initiallyPossible))
      return failure();
    if (*initiallyPossible == IncrementalSATSolver::kSAT) {
      recordCounterexampleBase(cube);
      return success();
    }
    if (*initiallyPossible != IncrementalSATSolver::kUNSAT)
      return system.module.emitError()
             << "PDR initial-state query returned unknown";
    addFrameClause(0, negateCube(cube));
    return success();
  }

  while (true) {
    auto predecessor = queryPredecessor(frame - 1, cube);
    if (failed(predecessor))
      return failure();
    if (predecessor->result == IncrementalSATSolver::kUNSAT) {
      bool usedCore = !predecessor->failedCube.empty();
      if (usedCore)
        ++numUsedCores;
      else
        ++numEmptyCores;
      Cube blockedCube =
          usedCore ? std::move(predecessor->failedCube) : Cube(cube);
      auto initiallyPossible = queryInitialCube(blockedCube);
      if (failed(initiallyPossible))
        return failure();
      if (*initiallyPossible == IncrementalSATSolver::kSAT) {
        if (usedCore)
          ++numInitialWeakCores;
        usedCore = false;
        blockedCube = std::move(cube);
        initiallyPossible = queryInitialCube(blockedCube);
        if (failed(initiallyPossible))
          return failure();
        if (*initiallyPossible == IncrementalSATSolver::kSAT) {
          recordCounterexampleBase(blockedCube);
          return success();
        }
      }
      if (*initiallyPossible != IncrementalSATSolver::kUNSAT)
        return system.module.emitError()
               << "PDR initial-state query returned unknown";

      auto generalized = generalizeCube(std::move(blockedCube), frame);
      if (failed(generalized))
        return failure();
      addBlockedCube(*generalized, frame);
      return success();
    }
    if (predecessor->result != IncrementalSATSolver::kSAT)
      return system.module.emitError()
             << "PDR predecessor query returned unknown";

    Cube targetCube(cube);
    if (failed(blockCube(std::move(predecessor->stateCube), frame - 1)))
      return failure();
    if (foundCounterexample) {
      appendCounterexampleState(targetCube, predecessor->inputValues);
      return success();
    }
  }
}

bool PDREngine::containsSubsumingClause(unsigned frame,
                                        const Clause &clause) const {
  for (const Clause &existing : frames[frame])
    if (subsumes(existing, clause))
      return true;
  return false;
}

bool PDREngine::isFixedPoint(unsigned frame) const {
  if (frame <= 1)
    return false;
  for (const Clause &clause : frames[frame - 1])
    if (!containsSubsumingClause(frame, clause))
      return false;
  return true;
}

std::optional<unsigned> PDREngine::findFixedPoint(unsigned maxFrame) const {
  for (unsigned frame = 2; frame <= maxFrame; ++frame)
    if (isFixedPoint(frame))
      return frame - 1;
  return std::nullopt;
}

void PDREngine::printBlockedCubeLimitUnknown(unsigned depth) {
  os << "pdr: unknown within depth " << llvm::utostr(depth)
     << " after reaching blocked-cube limit " << llvm::utostr(maxBlockedCubes)
     << "\n";
  dumpFramesForTrace();
}

void PDREngine::printConflictLimitUnknown(unsigned depth) {
  os << "pdr: unknown within depth " << llvm::utostr(depth)
     << " after SAT conflict limit " << llvm::utostr(conflictLimit) << "\n";
  dumpFramesForTrace();
}

FailureOr<bool> PDREngine::run() {
  frames.clear();
  frameContexts.clear();
  initialImageContext.reset();
  initialPredecessorContext.reset();
  globalInvariants.clear();
  counterexampleStates.clear();
  foundCounterexample = false;
  constraintsEmpty = false;
  initialImageEmpty = false;
  originalInitialClauses.clear();
  frames.emplace_back();
  frames.emplace_back();

  SmallVector<Clause> initialClauses;
  for (const StateInfo &state : system.states) {
    if (!state.initialValue)
      continue;
    APInt initial = state.initialValue->zextOrTrunc(state.width);
    for (unsigned bit = 0; bit != state.width; ++bit) {
      int lit = int(state.offset + bit + 1);
      initialClauses.push_back(Clause{initial[bit] ? lit : -lit});
    }
  }
  frames[0].append(initialClauses.begin(), initialClauses.end());
  originalInitialClauses = initialClauses;

  auto initialBad = queryBad(/*frame=*/0);
  if (failed(initialBad))
    return failure();
  if (initialBad->result == IncrementalSATSolver::kSAT) {
    recordCounterexampleBase(initialBad->stateCube);
    os << "pdr: counterexample found at depth 0\n";
    printCounterexampleTrace();
    return false;
  }
  if (initialBad->result != IncrementalSATSolver::kUNSAT) {
    if (conflictLimit >= 0) {
      printConflictLimitUnknown(/*depth=*/0);
      return false;
    }
    return system.module.emitError()
           << "PDR initial bad query returned unknown";
  }

  if (useInitialImage) {
    auto constraintUnits = computeConstraintUnitInvariants();
    if (failed(constraintUnits))
      return failure();
    if (constraintsEmpty) {
      os << "pdr: proven safe at depth 0\n";
      return true;
    }
    LDBG() << "pdr: constraint-unit-invariants=" << constraintUnits->size()
           << "\n";
    globalInvariants.append(constraintUnits->begin(), constraintUnits->end());

    auto initialImageUnits = computeInitialImageUnits(initialClauses);
    if (failed(initialImageUnits))
      return failure();
    if (initialImageEmpty) {
      os << "pdr: proven safe at depth 0\n";
      return true;
    }
    if (!initialImageUnits->empty()) {
      LDBG() << "pdr: initial-image-units=" << initialImageUnits->size()
             << "\n";
      frames[0] = std::move(*initialImageUnits);
      frameContexts.clear();
    }

    auto inductiveUnits = computeInductiveUnitInvariants(frames[0]);
    if (failed(inductiveUnits))
      return failure();
    LDBG() << "pdr: inductive-unit-invariants=" << inductiveUnits->size()
           << "\n";
    globalInvariants.append(inductiveUnits->begin(), inductiveUnits->end());
    frames[1].append(globalInvariants.begin(), globalInvariants.end());
  }

  for (unsigned depth = 1; depth <= maxDepth; ++depth) {
    while (true) {
      auto bad = queryBad(depth);
      if (failed(bad))
        return failure();
      if (bad->result == IncrementalSATSolver::kUNSAT)
        break;
      if (bad->result != IncrementalSATSolver::kSAT) {
        if (conflictLimit >= 0) {
          printConflictLimitUnknown(depth);
          return false;
        }
        return system.module.emitError()
               << "PDR bad-state query returned unknown";
      }

      if (failed(blockCube(std::move(bad->stateCube), depth)))
        return failure();
      if (foundCounterexample) {
        os << "pdr: counterexample found at depth " << llvm::utostr(depth)
           << "\n";
        printCounterexampleTrace();
        return false;
      }
      if (reachedBlockedCubeLimit()) {
        printBlockedCubeLimitUnknown(depth);
        return false;
      }
    }

    if (frames.size() == depth + 1) {
      frames.emplace_back();
      frames.back().append(globalInvariants.begin(), globalInvariants.end());
    }

    if (failed(propagateClauses(depth)))
      return failure();

    if (auto fixedPoint = findFixedPoint(depth + 1)) {
      os << "pdr: proven safe at depth " << llvm::utostr(*fixedPoint) << "\n";
      return true;
    }

    os << "pdr: depth " << llvm::utostr(depth) << " blocked "
       << llvm::utostr(frames[depth].size()) << " cubes\n";
  }

  os << "pdr: unknown within depth " << llvm::utostr(maxDepth) << "\n";
  dumpFramesForTrace();
  return false;
}

static void addInitialStateClauses(TransitionSystem &system,
                                   IncrementalSATSolver &solver,
                                   ArrayRef<int> stateVars) {
  for (const StateInfo &state : system.states) {
    if (!state.initialValue)
      continue;
    APInt initial = state.initialValue->zextOrTrunc(state.width);
    for (unsigned bit = 0; bit != state.width; ++bit) {
      int var = stateVars[state.offset + bit];
      solver.addClause({initial[bit] ? var : -var});
    }
  }
}

static APInt getStateValueFromModel(const StateInfo &state,
                                    ArrayRef<int> stateVars,
                                    IncrementalSATSolver &solver) {
  APInt value(state.width, 0);
  for (unsigned bit = 0; bit != state.width; ++bit) {
    int var = stateVars[state.offset + bit];
    if (solver.val(var) > 0)
      value.setBit(bit);
  }
  return value;
}

static void printInputAssignments(raw_ostream &os, unsigned frame,
                                  ArrayRef<InputAssignment> inputs,
                                  IncrementalSATSolver &solver) {
  if (inputs.empty())
    return;

  os << "pdr:   input " << llvm::utostr(frame) << ":";
  for (const InputAssignment &input : inputs) {
    APInt value(input.width, 0);
    for (auto [bit, var] : llvm::enumerate(input.bits))
      if (solver.val(var) > 0)
        value.setBit(bit);
    os << " ";
    printNamedStateValue(os, input.name, input.width, value);
  }
  os << "\n";
}

static void printBMCCounterexampleTrace(
    TransitionSystem &system, ArrayRef<SmallVector<int>> stateVars,
    ArrayRef<SmallVector<InputAssignment>> inputAssignments, unsigned depth,
    IncrementalSATSolver &solver, raw_ostream &os) {
  os << "pdr: counterexample trace:\n";
  for (unsigned frame = 0; frame <= depth; ++frame) {
    os << "pdr:   state " << llvm::utostr(frame) << ":";
    for (const StateInfo &state : system.states) {
      os << " ";
      APInt value = getStateValueFromModel(state, stateVars[frame], solver);
      printNamedStateValue(os, state.name, state.width, value);
    }
    os << "\n";
    if (frame < inputAssignments.size())
      printInputAssignments(os, frame, inputAssignments[frame], solver);
  }
}

static FailureOr<bool> runTransitionBMCPrecheck(TransitionSystem system,
                                                StringRef solverName,
                                                unsigned maxBound,
                                                raw_ostream &os,
                                                int64_t conflictLimit) {
  LDBG() << "bmc-precheck: max-bound=" << maxBound
         << " state-bits=" << system.numStateBits
         << " states=" << system.states.size() << "\n";
  if (maxBound == 0)
    return true;

  auto solver = createSATSolver(solverName);
  if (!solver)
    return system.module.emitError() << "no SAT solver available for BMC";
  solver->setConflictLimit(static_cast<int>(conflictLimit));

  SmallVector<SmallVector<int>> stateVars(maxBound);
  for (SmallVector<int> &frameVars : stateVars) {
    frameVars.reserve(system.numStateBits);
    for (unsigned bit = 0; bit != system.numStateBits; ++bit)
      frameVars.push_back(solver->newVar());
  }
  SmallVector<SmallVector<InputAssignment>> inputAssignments(maxBound);

  addInitialStateClauses(system, *solver, stateVars.front());
  {
    QueryEncoder initial(system, *solver, stateVars.front());
    if (failed(initial.encodeInitial()))
      return failure();
    initial.addInitial();
  }

  Cube fullState;
  fullState.reserve(system.numStateBits);
  for (unsigned bit = 0; bit != system.numStateBits; ++bit)
    fullState.push_back(int(bit + 1));

  for (unsigned depth = 0; depth != maxBound; ++depth) {
    QueryEncoder query(system, *solver, stateVars[depth]);
    if (failed(query.encodeConstraint()) || failed(query.encodeBad()))
      return failure();
    query.beginQuery();
    query.assumeConstraint();
    query.assumeBad();
    auto result = solver->solve({});
    LDBG() << "bmc-precheck: depth=" << depth
           << " result=" << getSATResultName(result) << "\n";
    if (result == IncrementalSATSolver::kUNKNOWN) {
      if (conflictLimit >= 0) {
        os << "pdr: unknown within depth " << depth
           << " after SAT conflict limit " << conflictLimit << "\n";
        return false;
      }
      return system.module.emitError() << "BMC precheck query returned unknown";
    }
    if (result == IncrementalSATSolver::kSAT) {
      inputAssignments[depth] = query.getInputAssignments();
      os << "pdr: counterexample found by BMC at depth " << depth << "\n";
      printBMCCounterexampleTrace(system, stateVars, inputAssignments, depth,
                                  *solver, os);
      return false;
    }

    if (depth + 1 == maxBound)
      break;

    QueryEncoder transition(system, *solver, stateVars[depth],
                            stateVars[depth + 1]);
    if (failed(transition.encodeConstraint()))
      return failure();
    transition.addConstraint();
    if (failed(transition.encodeTransitionForCube(fullState)))
      return failure();
    inputAssignments[depth] = transition.getInputAssignments();
    LDBG() << "bmc-precheck: encoded transition depth=" << depth << "\n";
  }

  return true;
}

FailureOr<bool> circt::fraig_lec::runBMCPrecheck(
    ModuleOp module, StringRef satSolver, unsigned maxBound, raw_ostream &os,
    unsigned maxDivModUnknownBits, int64_t conflictLimit) {
  SmallVector<verif::BoundedModelCheckingOp> bmcOps;
  module.walk([&](verif::BoundedModelCheckingOp op) { bmcOps.push_back(op); });
  if (bmcOps.empty())
    return module.emitError() << "BMC precheck found no verif.bmc operations";
  if (bmcOps.size() != 1)
    return module.emitError()
           << "BMC precheck currently supports exactly one verif.bmc operation";

  auto lowered = lowerBMCToTransitionModule(module, bmcOps.front());
  if (failed(lowered))
    return failure();
  bmcOps.front().erase();
  LDBG() << "bmc-precheck: lowered transition states="
         << lowered->initialValues.size()
         << " ops=" << countOperations(lowered->module) << "\n";

  if (failed(checkPDRSupportedAggregateWidths(lowered->module)))
    return failure();
  if (failed(checkPDRSupportedCombOps(lowered->module, maxDivModUnknownBits)))
    return failure();

  if (failed(normalizeTransitionModule(module, maxDivModUnknownBits)))
    return failure();

  auto system =
      collectTransitionSystem(lowered->module, lowered->initialValues,
                              lowered->stateNames, lowered->inputNames);
  if (failed(system))
    return failure();
  LDBG() << "bmc-precheck: collected state-bits=" << system->numStateBits
         << "\n";

  return runTransitionBMCPrecheck(std::move(*system), satSolver, maxBound, os,
                                  conflictLimit);
}

FailureOr<bool>
circt::fraig_lec::runBMCPDR(ModuleOp module, StringRef satSolver,
                            unsigned maxDepth, raw_ostream &os,
                            bool useInitialImage, unsigned maxDivModUnknownBits,
                            unsigned maxBlockedCubes, int64_t conflictLimit) {
  SmallVector<verif::BoundedModelCheckingOp> bmcOps;
  module.walk([&](verif::BoundedModelCheckingOp op) { bmcOps.push_back(op); });
  if (bmcOps.empty())
    return module.emitError() << "PDR found no verif.bmc operations";
  if (bmcOps.size() != 1)
    return module.emitError()
           << "PDR currently supports exactly one verif.bmc operation";

  auto lowered = lowerBMCToTransitionModule(module, bmcOps.front());
  if (failed(lowered))
    return failure();
  bmcOps.front().erase();
  LDBG() << "pdr: lowered transition states=" << lowered->initialValues.size()
         << " ops=" << countOperations(lowered->module) << "\n";

  if (failed(checkPDRSupportedAggregateWidths(lowered->module)))
    return failure();
  if (failed(checkPDRSupportedCombOps(lowered->module, maxDivModUnknownBits)))
    return failure();

  if (failed(normalizeTransitionModule(module, maxDivModUnknownBits)))
    return failure();

  auto system =
      collectTransitionSystem(lowered->module, lowered->initialValues,
                              lowered->stateNames, lowered->inputNames);
  if (failed(system))
    return failure();
  LDBG() << "pdr: collected transition state-bits=" << system->numStateBits
         << " states=" << system->states.size() << "\n";

  PDREngine engine(std::move(*system), satSolver, maxDepth, os, useInitialImage,
                   maxBlockedCubes, conflictLimit);
  return engine.run();
}

LogicalResult
circt::fraig_lec::printPDRTransition(ModuleOp module, raw_ostream &os,
                                     bool normalize,
                                     unsigned maxDivModUnknownBits) {
  SmallVector<verif::BoundedModelCheckingOp> bmcOps;
  module.walk([&](verif::BoundedModelCheckingOp op) { bmcOps.push_back(op); });
  if (bmcOps.empty())
    return module.emitError() << "PDR transition dump found no verif.bmc "
                                 "operations";
  if (bmcOps.size() != 1)
    return module.emitError()
           << "PDR transition dump currently supports exactly one verif.bmc "
              "operation";

  auto lowered = lowerBMCToTransitionModule(module, bmcOps.front());
  if (failed(lowered))
    return failure();
  bmcOps.front().erase();

  if (normalize) {
    if (failed(checkPDRSupportedAggregateWidths(lowered->module)))
      return failure();
    if (failed(checkPDRSupportedCombOps(lowered->module, maxDivModUnknownBits)))
      return failure();
    if (failed(normalizeTransitionModule(module, maxDivModUnknownBits)))
      return failure();
  }

  lowered->module->print(os);
  os << "\n";
  return success();
}
