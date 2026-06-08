//===- Counterexample.cpp - FRAIG LEC counterexample support ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CIRCTSynthFormal/Counterexample.h"
#include "CIRCTSynthFormal/MiterUtils.h"
#include "circt/Dialect/Comb/CombOps.h"
#include "circt/Dialect/HW/HWOps.h"
#include "circt/Dialect/HW/HWTypes.h"
#include "circt/Dialect/Synth/SynthOps.h"
#include "circt/Support/SATSolver.h"
#include "mlir/IR/Matchers.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/TypeSwitch.h"
#include <limits>

using namespace mlir;
using namespace circt;
using namespace circt::fraig_lec;

namespace {

static std::unique_ptr<IncrementalSATSolver>
createCounterexampleSolver(StringRef backend) {
  if (backend == "z3")
    return createZ3SATSolver();
  if (backend == "cadical")
    return createCadicalSATSolver();
  if (backend != "auto")
    return {};
  if (auto solver = createCadicalSATSolver())
    return solver;
  return createZ3SATSolver();
}

class CounterexampleEncoder {
public:
  explicit CounterexampleEncoder(hw::HWModuleOp miter,
                                 IncrementalSATSolver &solver)
      : miter(miter), solver(solver) {}

  LogicalResult solve(Value property, int64_t conflictLimit, raw_ostream &os);

private:
  FailureOr<int> getValueVar(Value value);
  FailureOr<int> getBitVar(Value value, unsigned bit);
  LogicalResult encodeValue(Value value);
  LogicalResult encodeBooleanLogic(synth::BooleanLogicOpInterface op,
                                   Value result);
  LogicalResult encodeCombLogic(Operation *op, Value result);
  LogicalResult encodeConstant(Value result, const APInt &value);
  int createVar(Value value);
  int createPortBitVar(BlockArgument arg, unsigned bit);
  FailureOr<unsigned> getKnownBitWidth(Type type);
  void printModel(raw_ostream &os);

  hw::HWModuleOp miter;
  IncrementalSATSolver &solver;
  DenseMap<Value, int> valueVars;
  DenseMap<BlockArgument, SmallVector<int>> portBitVars;
  DenseSet<Value> encodedValues;
  DenseSet<Value> encodingValues;
};

int CounterexampleEncoder::createVar(Value value) {
  auto [it, inserted] = valueVars.try_emplace(value, 0);
  if (inserted)
    it->second = solver.newVar();
  return it->second;
}

int CounterexampleEncoder::createPortBitVar(BlockArgument arg, unsigned bit) {
  auto width = getKnownBitWidth(arg.getType());
  assert(succeeded(width) && bit < *width && "invalid port bit");
  auto &bits = portBitVars[arg];
  if (bits.empty())
    bits.resize(*width, 0);
  if (!bits[bit])
    bits[bit] = solver.newVar();
  if (*width == 1)
    valueVars.try_emplace(arg, bits[bit]);
  return bits[bit];
}

FailureOr<unsigned> CounterexampleEncoder::getKnownBitWidth(Type type) {
  int64_t width = hw::getBitWidth(type);
  if (width < 0 || width > std::numeric_limits<unsigned>::max())
    return failure();
  return static_cast<unsigned>(width);
}

FailureOr<int> CounterexampleEncoder::getBitVar(Value value, unsigned bit) {
  if (auto arg = dyn_cast<BlockArgument>(value)) {
    auto width = getKnownBitWidth(arg.getType());
    if (failed(width) || bit >= *width)
      return miter.emitError() << "cannot encode counterexample bit access for "
                                  "unknown-width miter input";
    return createPortBitVar(arg, bit);
  }

  APInt constant;
  if (matchPattern(value, m_ConstantInt(&constant))) {
    if (bit >= constant.getBitWidth())
      return miter.emitError() << "cannot encode out-of-range constant bit";
    int var = solver.newVar();
    solver.addClause({constant[bit] ? var : -var});
    return var;
  }

  if (bit == 0 && value.getType().isInteger(1))
    return getValueVar(value);

  if (auto bitcast = value.getDefiningOp<hw::BitcastOp>())
    return getBitVar(bitcast.getInput(), bit);

  return miter.emitError() << "counterexample extraction currently requires "
                              "bit-blasted logic";
}

FailureOr<int> CounterexampleEncoder::getValueVar(Value value) {
  if (auto it = valueVars.find(value); it != valueVars.end())
    return it->second;

  if (auto arg = dyn_cast<BlockArgument>(value)) {
    if (!arg.getType().isInteger(1))
      return miter.emitError()
             << "counterexample query expected a single-bit value";
    return createPortBitVar(arg, 0);
  }

  if (failed(encodeValue(value)))
    return failure();
  auto it = valueVars.find(value);
  if (it == valueVars.end())
    return miter.emitError() << "counterexample encoder did not create a SAT "
                                "variable for a value";
  return it->second;
}

LogicalResult CounterexampleEncoder::encodeConstant(Value result,
                                                    const APInt &value) {
  if (!result.getType().isInteger(1))
    return miter.emitError()
           << "counterexample encoder only supports single-bit logic";
  int var = createVar(result);
  solver.addClause({value.isZero() ? -var : var});
  return success();
}

LogicalResult
CounterexampleEncoder::encodeBooleanLogic(synth::BooleanLogicOpInterface op,
                                          Value result) {
  int outVar = createVar(result);
  SmallVector<int> inputVars;
  inputVars.reserve(op->getNumOperands());
  for (Value input : op.getInputs()) {
    auto inputVar = getValueVar(input);
    if (failed(inputVar))
      return failure();
    inputVars.push_back(*inputVar);
  }

  auto addClause = [&](ArrayRef<int> clause) { solver.addClause(clause); };
  op.emitCNF(outVar, inputVars, addClause, [&]() { return solver.newVar(); });
  return success();
}

LogicalResult CounterexampleEncoder::encodeCombLogic(Operation *op,
                                                     Value result) {
  int outVar = createVar(result);
  SmallVector<int> inputVars;
  inputVars.reserve(op->getNumOperands());
  for (Value input : op->getOperands()) {
    auto inputVar = getValueVar(input);
    if (failed(inputVar))
      return failure();
    inputVars.push_back(*inputVar);
  }

  auto addClause = [&](ArrayRef<int> clause) { solver.addClause(clause); };
  if (isa<comb::AndOp>(op)) {
    addAndClauses(outVar, inputVars, addClause);
    return success();
  }
  if (isa<comb::OrOp>(op)) {
    addOrClauses(outVar, inputVars, addClause);
    return success();
  }
  if (isa<comb::XorOp>(op)) {
    addParityClauses(outVar, inputVars, addClause,
                     [&]() { return solver.newVar(); });
    return success();
  }
  return failure();
}

LogicalResult CounterexampleEncoder::encodeValue(Value value) {
  if (encodedValues.contains(value))
    return success();
  if (!encodingValues.insert(value).second)
    return miter.emitError() << "cycle in counterexample SAT encoding";
  llvm::scope_exit cleanup([&] { encodingValues.erase(value); });

  Operation *op = value.getDefiningOp();
  if (!op)
    return miter.emitError()
           << "counterexample query expected a mapped miter value";

  APInt constant;
  if (matchPattern(value, m_ConstantInt(&constant))) {
    if (failed(encodeConstant(value, constant)))
      return failure();
    encodedValues.insert(value);
    return success();
  }

  if (auto extract = dyn_cast<comb::ExtractOp>(op)) {
    if (!value.getType().isInteger(1))
      return miter.emitError()
             << "counterexample encoder only supports single-bit extracts";
    auto sourceBit = getBitVar(extract.getInput(), extract.getLowBit());
    if (failed(sourceBit))
      return failure();
    valueVars[value] = *sourceBit;
    encodedValues.insert(value);
    return success();
  }

  if (auto bitcast = dyn_cast<hw::BitcastOp>(op)) {
    if (!value.getType().isInteger(1))
      return miter.emitError()
             << "counterexample encoder only supports single-bit bitcasts";
    auto sourceBit = getBitVar(bitcast.getInput(), 0);
    if (failed(sourceBit))
      return failure();
    valueVars[value] = *sourceBit;
    encodedValues.insert(value);
    return success();
  }

  if (auto logicOp = dyn_cast<synth::BooleanLogicOpInterface>(op)) {
    if (failed(encodeBooleanLogic(logicOp, value)))
      return failure();
    encodedValues.insert(value);
    return success();
  }

  if (isa<comb::AndOp, comb::OrOp, comb::XorOp>(op)) {
    if (failed(encodeCombLogic(op, value)))
      return failure();
    encodedValues.insert(value);
    return success();
  }

  return op->emitError()
         << "unsupported operation in counterexample SAT encoding";
}

LogicalResult CounterexampleEncoder::solve(Value property,
                                           int64_t conflictLimit,
                                           raw_ostream &os) {
  auto propertyVar = getValueVar(property);
  if (failed(propertyVar))
    return failure();

  solver.setConflictLimit(static_cast<int>(conflictLimit));
  auto result = solver.solve({*propertyVar});
  if (result == IncrementalSATSolver::kUNSAT)
    return success();
  if (result != IncrementalSATSolver::kSAT)
    return miter.emitError() << "counterexample SAT query returned unknown";

  printModel(os);
  return success();
}

void CounterexampleEncoder::printModel(raw_ostream &os) {
  os << "    counterexample:\n";
  auto counterexampleNames =
      miter->getAttrOfType<ArrayAttr>(kCounterexampleNamesAttrName);
  for (auto port : miter.getPortList()) {
    if (!port.isInput())
      continue;
    StringRef name = port.name.getValue();
    if (counterexampleNames && port.argNum < counterexampleNames.size())
      if (auto nameAttr =
              dyn_cast<StringAttr>(counterexampleNames[port.argNum]))
        name = nameAttr.getValue();

    BlockArgument arg = miter.getBodyBlock()->getArgument(port.argNum);
    auto width = getKnownBitWidth(arg.getType());
    if (failed(width)) {
      os << "      " << name << " = <unknown-width>\n";
      continue;
    }

    APInt value(*width, 0);
    auto bitIt = portBitVars.find(arg);
    if (bitIt != portBitVars.end()) {
      for (auto [bit, var] : llvm::enumerate(bitIt->second)) {
        if (!var)
          continue;
        if (solver.val(var) > 0)
          value.setBit(bit);
      }
    }

    SmallString<32> str;
    value.toString(str, 16, /*Signed=*/false, /*formatAsCLiteral=*/true);
    os << "      " << name << " = " << str << "\n";
  }
}

} // namespace

LogicalResult circt::fraig_lec::printCounterexample(hw::HWModuleOp miter,
                                                    Value property,
                                                    StringRef satSolver,
                                                    int64_t conflictLimit,
                                                    raw_ostream &os) {
  auto solver = createCounterexampleSolver(satSolver);
  if (!solver)
    return miter.emitError() << "unsupported or unavailable SAT solver '"
                             << satSolver << "' for counterexample generation";

  CounterexampleEncoder encoder(miter, *solver);
  return encoder.solve(property, conflictLimit, os);
}
