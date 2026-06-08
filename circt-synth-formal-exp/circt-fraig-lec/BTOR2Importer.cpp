//===- BTOR2Importer.cpp - Materialize BTOR2 as verif.bmc ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "BTOR2Importer.h"
#include "circt/Dialect/Comb/CombOps.h"
#include "circt/Dialect/HW/HWOps.h"
#include "circt/Dialect/Seq/SeqOps.h"
#include "circt/Dialect/Verif/VerifOps.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/MemoryBuffer.h"

using namespace mlir;
using namespace circt;
using namespace circt::fraig_lec;

static constexpr StringRef kInitialOnlyAttr = "fraig_lec.initial_only";

namespace {

struct BTOR2Line {
  unsigned sourceLine = 0;
  uint64_t id = 0;
  std::string op;
  SmallVector<std::string> tokens;
};

struct NodeInfo {
  BTOR2Line line;
  Type type;
  unsigned width = 0;
  std::string name;
};

struct StateInfo {
  NodeInfo node;
  std::optional<APInt> initialValue;
  int64_t initialRef = 0;
  int64_t next = 0;
};

struct Problem {
  struct SortInfo {
    enum Kind { BitVector, Array } kind = BitVector;
    unsigned sourceLine = 0;
    unsigned width = 0;
    uint64_t indexSort = 0;
    uint64_t elementSort = 0;
  };

  DenseMap<uint64_t, SortInfo> sorts;
  DenseMap<uint64_t, Type> sortTypes;
  DenseMap<uint64_t, Type> types;
  DenseMap<uint64_t, BTOR2Line> lines;
  SmallVector<NodeInfo> inputs;
  SmallVector<StateInfo> states;
  SmallVector<std::pair<int64_t, unsigned>> constraints;
  SmallVector<std::pair<int64_t, unsigned>> bads;
};

struct Materializer {
  Problem &problem;
  MLIRContext &context;
  OpBuilder builder;
  Location loc;
  DenseMap<uint64_t, Value> values;
  SmallPtrSet<uint64_t, 16> materializing;

  Materializer(Problem &problem, MLIRContext &context, Location loc)
      : problem(problem), context(context), builder(&context), loc(loc) {}

  LogicalResult materializeCircuit(verif::BoundedModelCheckingOp bmcOp);

private:
  InFlightDiagnostic emitLineError(const BTOR2Line &line);
  FailureOr<Value> materialize(uint64_t id);
  FailureOr<Value> materializeRef(int64_t ref);
  FailureOr<Value> materializeUnary(const BTOR2Line &line, StringRef op,
                                    Value input);
  FailureOr<Value> materializeBinary(const BTOR2Line &line, StringRef op,
                                     Value lhs, Value rhs);
  FailureOr<Value> getOperand(const BTOR2Line &line, unsigned index);
  FailureOr<Value> createNot(Value value);
  FailureOr<Value> createRedOr(Value value);
  FailureOr<Value> createRedAnd(Value value);
  FailureOr<Value> createRedXor(Value value);
  FailureOr<Value> createCompare(const BTOR2Line &line,
                                 comb::ICmpPredicate predicate, Value lhs,
                                 Value rhs);
  FailureOr<Value> createBtorDivMod(StringRef op, Value lhs, Value rhs);
};

} // namespace

static InFlightDiagnostic emitError(MLIRContext &context, Twine message) {
  return mlir::emitError(UnknownLoc::get(&context)) << message;
}

static FailureOr<uint64_t> parseUInt(MLIRContext &context, StringRef spelling,
                                     Twine what) {
  uint64_t value;
  if (spelling.getAsInteger(10, value))
    return emitError(context, "invalid " + what + " '" + spelling + "'");
  return value;
}

static FailureOr<int64_t> parseRef(MLIRContext &context, StringRef spelling,
                                   Twine what) {
  int64_t value;
  if (spelling.getAsInteger(10, value) || value == 0)
    return emitError(context, "invalid " + what + " '" + spelling + "'");
  return value;
}

static uint64_t getRefID(int64_t ref) {
  return ref < 0 ? uint64_t(-ref) : uint64_t(ref);
}

static FailureOr<APInt> parseConstant(MLIRContext &context, unsigned width,
                                      StringRef spelling, unsigned radix) {
  if (width == 0)
    return emitError(context, "zero-width BTOR2 constants are not supported");
  bool negative = spelling.consume_front("-");
  APInt value(width, spelling, radix);
  if (negative)
    value = -value;
  return value;
}

static std::string getDefaultName(StringRef prefix, uint64_t id) {
  return (prefix + Twine(id)).str();
}

static StateInfo *findState(Problem &problem, uint64_t id) {
  auto it = llvm::find_if(problem.states, [&](const StateInfo &state) {
    return state.node.line.id == id;
  });
  if (it == problem.states.end())
    return nullptr;
  return &*it;
}

static FailureOr<Type> getSortType(MLIRContext &context, Problem &problem,
                                   uint64_t sortId) {
  if (Type type = problem.sortTypes.lookup(sortId))
    return type;

  auto sortIt = problem.sorts.find(sortId);
  if (sortIt == problem.sorts.end())
    return emitError(context, "unknown sort id " + Twine(sortId));

  const Problem::SortInfo &sort = sortIt->second;
  if (sort.kind == Problem::SortInfo::BitVector) {
    Type type = IntegerType::get(&context, sort.width);
    problem.sortTypes[sortId] = type;
    return type;
  }

  auto indexType = getSortType(context, problem, sort.indexSort);
  auto elementType = getSortType(context, problem, sort.elementSort);
  if (failed(indexType) || failed(elementType))
    return failure();

  auto indexIntegerType = dyn_cast<IntegerType>(*indexType);
  if (!indexIntegerType)
    return emitError(context, "BTOR2 array index sort " +
                                  Twine(sort.indexSort) +
                                  " must be a bit-vector sort");
  unsigned indexWidth = indexIntegerType.getWidth();
  if (indexWidth >= 32)
    return emitError(
        context, "BTOR2 array index width " + Twine(indexWidth) +
                     " is too large to materialize as an HW array on line " +
                     Twine(sort.sourceLine));

  Type type = hw::ArrayType::get(*elementType, uint64_t{1} << indexWidth);
  problem.sortTypes[sortId] = type;
  return type;
}

static unsigned getIntegerWidth(Type type) {
  if (auto intType = dyn_cast<IntegerType>(type))
    return intType.getWidth();
  return 0;
}

static APInt boolAPInt(bool value) { return APInt(1, value ? 1 : 0); }

static APInt signedDivByZeroAPInt(const APInt &lhs) {
  if (lhs.isNegative())
    return APInt(lhs.getBitWidth(), 1);
  return APInt::getAllOnes(lhs.getBitWidth());
}

static FailureOr<std::optional<APInt>>
evaluateConstantRef(MLIRContext &context, Problem &problem, int64_t ref,
                    DenseSet<uint64_t> &visiting);

static FailureOr<std::optional<APInt>>
evaluateConstantID(MLIRContext &context, Problem &problem, uint64_t id,
                   DenseSet<uint64_t> &visiting) {
  if (!visiting.insert(id).second)
    return emitError(context,
                     "cyclic BTOR2 init expression at id " + Twine(id));
  llvm::scope_exit cleanup([&] { visiting.erase(id); });

  if (auto *state = findState(problem, id)) {
    if (state->initialValue)
      return state->initialValue;
    if (!state->initialRef)
      return std::optional<APInt>();
    auto value =
        evaluateConstantRef(context, problem, state->initialRef, visiting);
    if (failed(value) || !*value)
      return value;
    state->initialValue = value->value().zextOrTrunc(state->node.width);
    return state->initialValue;
  }

  auto lineIt = problem.lines.find(id);
  if (lineIt == problem.lines.end())
    return emitError(context, "unknown BTOR2 value id " + Twine(id));

  const BTOR2Line &line = lineIt->second;
  Type type = problem.types.lookup(id);
  unsigned width = getIntegerWidth(type);
  StringRef op = line.op;

  if (op == "input" || op == "state")
    return std::optional<APInt>();

  if (op == "zero")
    return std::optional<APInt>(APInt(width, 0));
  if (op == "one")
    return std::optional<APInt>(APInt(width, 1));
  if (op == "ones")
    return std::optional<APInt>(APInt::getAllOnes(width));
  if (op == "constd" || op == "consth" || op == "const") {
    if (line.tokens.size() < 2)
      return emitError(context, "missing constant value on line " +
                                    Twine(line.sourceLine));
    auto value = parseConstant(context, width, line.tokens[1],
                               op == "consth"  ? 16
                               : op == "const" ? 2
                                               : 10);
    if (failed(value))
      return failure();
    return std::optional<APInt>(*value);
  }

  auto getOperand = [&](unsigned index) -> FailureOr<std::optional<APInt>> {
    if (line.tokens.size() <= index)
      return emitError(context,
                       "missing operand on line " + Twine(line.sourceLine));
    auto ref = parseRef(context, line.tokens[index], "operand id");
    if (failed(ref))
      return failure();
    return evaluateConstantRef(context, problem, *ref, visiting);
  };

  if (op == "slice") {
    auto input = getOperand(1);
    if (failed(input) || !*input)
      return input;
    if (line.tokens.size() < 4)
      return emitError(context, "missing slice bounds on line " +
                                    Twine(line.sourceLine));
    auto high = parseUInt(context, line.tokens[2], "slice high bit");
    auto low = parseUInt(context, line.tokens[3], "slice low bit");
    if (failed(high) || failed(low))
      return failure();
    return std::optional<APInt>(
        input->value().extractBits(*high - *low + 1, *low));
  }

  if (op == "ite") {
    auto cond = getOperand(1);
    if (failed(cond) || !*cond)
      return cond;
    return getOperand(cond->value().isZero() ? 3 : 2);
  }

  auto lhs = getOperand(1);
  if (failed(lhs) || !*lhs)
    return lhs;

  if (op == "not")
    return std::optional<APInt>(~lhs->value());
  if (op == "neg")
    return std::optional<APInt>(-lhs->value());
  if (op == "redor")
    return std::optional<APInt>(boolAPInt(!lhs->value().isZero()));
  if (op == "redand")
    return std::optional<APInt>(boolAPInt(lhs->value().isAllOnes()));
  if (op == "redxor")
    return std::optional<APInt>(boolAPInt(lhs->value().popcount() & 1));
  if (op == "uext" || op == "sext") {
    if (line.tokens.size() < 3)
      return emitError(context, "missing extension amount on line " +
                                    Twine(line.sourceLine));
    return std::optional<APInt>(op == "sext" ? lhs->value().sext(width)
                                             : lhs->value().zext(width));
  }

  auto rhs = getOperand(2);
  if (failed(rhs) || !*rhs)
    return rhs;

  APInt left =
      lhs->value().zextOrTrunc(width ? width : lhs->value().getBitWidth());
  APInt right = rhs->value().zextOrTrunc(left.getBitWidth());

  if (op == "concat") {
    unsigned concatWidth =
        lhs->value().getBitWidth() + rhs->value().getBitWidth();
    APInt result =
        lhs->value().zext(concatWidth).shl(rhs->value().getBitWidth());
    result |= rhs->value().zext(concatWidth);
    return std::optional<APInt>(result);
  }

  if (op == "and")
    return std::optional<APInt>(left & right);
  if (op == "nand")
    return std::optional<APInt>(~(left & right));
  if (op == "or")
    return std::optional<APInt>(left | right);
  if (op == "xor")
    return std::optional<APInt>(left ^ right);
  if (op == "xnor")
    return std::optional<APInt>(~(left ^ right));
  if (op == "add")
    return std::optional<APInt>(left + right);
  if (op == "sub")
    return std::optional<APInt>(left - right);
  if (op == "mul")
    return std::optional<APInt>(left * right);
  if (op == "udiv")
    return std::optional<APInt>(right.isZero()
                                    ? APInt::getAllOnes(left.getBitWidth())
                                    : left.udiv(right));
  if (op == "sdiv")
    return std::optional<APInt>(right.isZero() ? signedDivByZeroAPInt(left)
                                               : left.sdiv(right));
  if (op == "urem")
    return std::optional<APInt>(right.isZero() ? left : left.urem(right));
  if (op == "srem" || op == "smod")
    return std::optional<APInt>(right.isZero() ? left : left.srem(right));
  if (op == "sll" || op == "srl" || op == "sra") {
    uint64_t amount = right.getLimitedValue(left.getBitWidth());
    if (amount >= left.getBitWidth()) {
      if (op == "sra" && left.isNegative())
        return std::optional<APInt>(APInt::getAllOnes(left.getBitWidth()));
      return std::optional<APInt>(APInt(left.getBitWidth(), 0));
    }
    if (op == "sll")
      return std::optional<APInt>(left.shl(amount));
    if (op == "srl")
      return std::optional<APInt>(left.lshr(amount));
    return std::optional<APInt>(left.ashr(amount));
  }

  if (op == "eq")
    return std::optional<APInt>(boolAPInt(left == right));
  if (op == "neq")
    return std::optional<APInt>(boolAPInt(left != right));
  if (op == "ult")
    return std::optional<APInt>(boolAPInt(left.ult(right)));
  if (op == "ulte")
    return std::optional<APInt>(boolAPInt(left.ule(right)));
  if (op == "ugt")
    return std::optional<APInt>(boolAPInt(left.ugt(right)));
  if (op == "ugte")
    return std::optional<APInt>(boolAPInt(left.uge(right)));
  if (op == "slt")
    return std::optional<APInt>(boolAPInt(left.slt(right)));
  if (op == "slte")
    return std::optional<APInt>(boolAPInt(left.sle(right)));
  if (op == "sgt")
    return std::optional<APInt>(boolAPInt(left.sgt(right)));
  if (op == "sgte")
    return std::optional<APInt>(boolAPInt(left.sge(right)));
  if (op == "implies")
    return std::optional<APInt>(boolAPInt(left.isZero() || !right.isZero()));

  return std::optional<APInt>();
}

static FailureOr<std::optional<APInt>>
evaluateConstantRef(MLIRContext &context, Problem &problem, int64_t ref,
                    DenseSet<uint64_t> &visiting) {
  auto value = evaluateConstantID(context, problem, getRefID(ref), visiting);
  if (failed(value) || !*value || ref > 0)
    return value;
  value->value().flipAllBits();
  return value;
}

static LogicalResult resolveInitialValues(MLIRContext &context,
                                          Problem &problem) {
  for (StateInfo &state : problem.states) {
    if (!state.initialRef)
      continue;
    DenseSet<uint64_t> visiting;
    auto value =
        evaluateConstantRef(context, problem, state.initialRef, visiting);
    if (failed(value))
      return failure();
    if (!*value)
      continue;
    state.initialValue = value->value().zextOrTrunc(state.node.width);
  }
  return success();
}

static LogicalResult parseBTOR2(StringRef input, MLIRContext &context,
                                Problem &problem) {
  SmallVector<StringRef> physicalLines;
  input.split(physicalLines, '\n');

  for (auto [index, physicalLine] : llvm::enumerate(physicalLines)) {
    StringRef line = physicalLine.split(';').first.trim();
    if (line.empty())
      continue;

    SmallVector<StringRef> pieces;
    llvm::SplitString(line, pieces);
    if (pieces.size() < 2)
      return emitError(context, "invalid BTOR2 line " + Twine(index + 1));

    auto id = parseUInt(context, pieces[0], "line id");
    if (failed(id))
      return failure();

    BTOR2Line parsed;
    parsed.sourceLine = index + 1;
    parsed.id = *id;
    parsed.op = pieces[1].str();
    for (StringRef piece : ArrayRef(pieces).drop_front(2))
      parsed.tokens.push_back(piece.str());

    if (!problem.lines.insert({*id, parsed}).second)
      return emitError(context, "duplicate BTOR2 line id " + Twine(*id) +
                                    " on line " + Twine(index + 1));

    if (parsed.op != "sort")
      continue;
    if (pieces.size() < 4)
      return emitError(context, "malformed sort on line " + Twine(index + 1));

    Problem::SortInfo sort;
    sort.sourceLine = index + 1;
    if (pieces[2] == "bitvec") {
      if (pieces.size() != 4)
        return emitError(context, "malformed bit-vector sort on line " +
                                      Twine(index + 1));
      auto width = parseUInt(context, pieces[3], "sort width");
      if (failed(width))
        return failure();
      if (*width == 0 || *width > std::numeric_limits<unsigned>::max())
        return emitError(context,
                         "unsupported BTOR2 bit-vector width on line " +
                             Twine(index + 1));
      sort.kind = Problem::SortInfo::BitVector;
      sort.width = *width;
      problem.sorts[*id] = sort;
      continue;
    }

    if (pieces[2] == "array") {
      if (pieces.size() != 5)
        return emitError(context,
                         "malformed array sort on line " + Twine(index + 1));
      auto indexSort = parseUInt(context, pieces[3], "array index sort id");
      auto elementSort = parseUInt(context, pieces[4], "array element sort id");
      if (failed(indexSort) || failed(elementSort))
        return failure();
      sort.kind = Problem::SortInfo::Array;
      sort.indexSort = *indexSort;
      sort.elementSort = *elementSort;
      problem.sorts[*id] = sort;
      continue;
    }

    return emitError(context, "unsupported BTOR2 sort kind '" + pieces[2] +
                                  "' on line " + Twine(index + 1));
  }

  for (auto [index, physicalLine] : llvm::enumerate(physicalLines)) {
    StringRef line = physicalLine.split(';').first.trim();
    if (line.empty())
      continue;

    SmallVector<StringRef> pieces;
    llvm::SplitString(line, pieces);
    if (pieces.size() < 2)
      return emitError(context, "invalid BTOR2 line " + Twine(index + 1));

    auto id = parseUInt(context, pieces[0], "line id");
    if (failed(id))
      return failure();

    BTOR2Line parsed;
    parsed.sourceLine = index + 1;
    parsed.id = *id;
    parsed.op = pieces[1].str();
    for (StringRef piece : ArrayRef(pieces).drop_front(2))
      parsed.tokens.push_back(piece.str());

    StringRef op = parsed.op;
    if (op == "sort") {
      continue;
    }

    if (op == "output") {
      continue;
    }

    if (op == "bad" || op == "constraint") {
      if (pieces.size() < 3)
        return emitError(context,
                         "missing operand on line " + Twine(index + 1));
      auto operand = parseRef(context, pieces[2], "operand id");
      if (failed(operand))
        return failure();
      if (op == "bad")
        problem.bads.push_back({*operand, *id});
      else
        problem.constraints.push_back({*operand, *id});
      continue;
    }

    if (pieces.size() < 3)
      return emitError(context,
                       "missing sort operand on line " + Twine(index + 1));
    auto sort = parseUInt(context, pieces[2], "sort id");
    if (failed(sort))
      return failure();
    auto type = getSortType(context, problem, *sort);
    if (failed(type))
      return failure();
    unsigned width = getIntegerWidth(*type);
    problem.types[*id] = *type;

    if (op == "input" || op == "state") {
      NodeInfo node{parsed, *type, width,
                    pieces.size() >= 4 ? pieces[3].str()
                                       : getDefaultName(op, *id)};
      if (op == "input")
        problem.inputs.push_back(node);
      else
        problem.states.push_back({node, std::nullopt, 0});
      continue;
    }

    if (op == "init") {
      if (pieces.size() < 5)
        return emitError(context, "malformed init on line " + Twine(index + 1));
      auto stateId = parseUInt(context, pieces[3], "state id");
      auto valueId = parseRef(context, pieces[4], "init value id");
      if (failed(stateId) || failed(valueId))
        return failure();
      auto valueLineIt = problem.lines.find(getRefID(*valueId));
      if (valueLineIt == problem.lines.end())
        return emitError(context, "unknown init value id " + Twine(*valueId) +
                                      " on line " + Twine(index + 1));
      auto stateIt = llvm::find_if(problem.states, [&](const StateInfo &state) {
        return state.node.line.id == *stateId;
      });
      if (stateIt == problem.states.end())
        return emitError(context, "unknown init state id " + Twine(*stateId) +
                                      " on line " + Twine(index + 1));
      if (!stateIt->node.width)
        return emitError(context,
                         "only bit-vector BTOR2 init values are supported on "
                         "line " +
                             Twine(index + 1));
      stateIt->initialRef = *valueId;
      continue;
    }

    if (op == "next") {
      if (pieces.size() < 5)
        return emitError(context, "malformed next on line " + Twine(index + 1));
      auto stateId = parseUInt(context, pieces[3], "state id");
      auto valueId = parseRef(context, pieces[4], "next value id");
      if (failed(stateId) || failed(valueId))
        return failure();
      auto stateIt = llvm::find_if(problem.states, [&](const StateInfo &state) {
        return state.node.line.id == *stateId;
      });
      if (stateIt == problem.states.end())
        return emitError(context, "unknown next state id " + Twine(*stateId) +
                                      " on line " + Twine(index + 1));
      stateIt->next = *valueId;
      continue;
    }
  }

  if (problem.bads.empty())
    return emitError(context, "BTOR2 input contains no bad properties");
  if (failed(resolveInitialValues(context, problem)))
    return failure();
  return success();
}

InFlightDiagnostic Materializer::emitLineError(const BTOR2Line &line) {
  return mlir::emitError(loc)
         << "unsupported BTOR2 '" << line.op << "' on line " << line.sourceLine;
}

FailureOr<Value> Materializer::getOperand(const BTOR2Line &line,
                                          unsigned index) {
  if (line.tokens.size() <= index)
    return emitLineError(line) << ": missing operand";
  auto operand = parseRef(context, line.tokens[index], "operand id");
  if (failed(operand))
    return failure();
  return materializeRef(*operand);
}

FailureOr<Value> Materializer::materializeRef(int64_t ref) {
  auto value = materialize(getRefID(ref));
  if (failed(value) || ref > 0)
    return value;
  return createNot(*value);
}

FailureOr<Value> Materializer::createNot(Value value) {
  if (!value.getType().isInteger())
    return failure();
  APInt ones(value.getType().getIntOrFloatBitWidth(), -1, true);
  Value allOnes = hw::ConstantOp::create(builder, loc, ones);
  return builder.createOrFold<comb::XorOp>(loc, value, allOnes);
}

FailureOr<Value> Materializer::createRedOr(Value value) {
  auto width = value.getType().getIntOrFloatBitWidth();
  if (width == 1)
    return value;
  SmallVector<Value> bits;
  bits.reserve(width);
  for (unsigned bit = 0; bit != width; ++bit)
    bits.push_back(comb::ExtractOp::create(builder, loc, value, bit, 1));
  return builder.createOrFold<comb::OrOp>(loc, bits, true);
}

FailureOr<Value> Materializer::createRedAnd(Value value) {
  auto width = value.getType().getIntOrFloatBitWidth();
  if (width == 1)
    return value;
  SmallVector<Value> bits;
  bits.reserve(width);
  for (unsigned bit = 0; bit != width; ++bit)
    bits.push_back(comb::ExtractOp::create(builder, loc, value, bit, 1));
  return builder.createOrFold<comb::AndOp>(loc, bits, true);
}

FailureOr<Value> Materializer::createRedXor(Value value) {
  auto width = value.getType().getIntOrFloatBitWidth();
  if (width == 1)
    return value;
  SmallVector<Value> bits;
  bits.reserve(width);
  for (unsigned bit = 0; bit != width; ++bit)
    bits.push_back(comb::ExtractOp::create(builder, loc, value, bit, 1));
  return builder.createOrFold<comb::XorOp>(loc, bits, true);
}

FailureOr<Value> Materializer::createCompare(const BTOR2Line &line,
                                             comb::ICmpPredicate predicate,
                                             Value lhs, Value rhs) {
  if (lhs.getType().isInteger())
    return builder.createOrFold<comb::ICmpOp>(loc, predicate, lhs, rhs);

  if (!hw::type_isa<hw::ArrayType>(lhs.getType()) ||
      (predicate != comb::ICmpPredicate::eq &&
       predicate != comb::ICmpPredicate::ne))
    return emitLineError(line)
           << "unsupported non-integer BTOR2 comparison '" << line.op << "'";

  constexpr int64_t maxScalarBitWidth = (1 << 24) - 1;
  int64_t width = hw::getBitWidth(lhs.getType());
  if (width <= 0 || width > maxScalarBitWidth)
    return emitLineError(line)
           << "BTOR2 array comparison would create a " << width
           << "-bit scalar, exceeding the MLIR integer width limit";

  auto intType = builder.getIntegerType(width);
  lhs = hw::BitcastOp::create(builder, loc, intType, lhs);
  rhs = hw::BitcastOp::create(builder, loc, intType, rhs);
  return builder.createOrFold<comb::ICmpOp>(loc, predicate, lhs, rhs);
}

FailureOr<Value> Materializer::createBtorDivMod(StringRef op, Value lhs,
                                                Value rhs) {
  Value result;
  if (op == "udiv")
    result = builder.createOrFold<comb::DivUOp>(loc, lhs, rhs);
  else if (op == "sdiv")
    result = builder.createOrFold<comb::DivSOp>(loc, lhs, rhs);
  else if (op == "urem")
    result = builder.createOrFold<comb::ModUOp>(loc, lhs, rhs);
  else
    result = builder.createOrFold<comb::ModSOp>(loc, lhs, rhs);

  auto width = rhs.getType().getIntOrFloatBitWidth();
  Value zero = hw::ConstantOp::create(builder, loc, APInt(width, 0));
  auto isZero = builder.createOrFold<comb::ICmpOp>(loc, comb::ICmpPredicate::eq,
                                                   rhs, zero);
  if (op == "urem" || op == "srem" || op == "smod")
    return builder.createOrFold<comb::MuxOp>(loc, isZero, lhs, result);
  if (op == "sdiv") {
    Value sign = comb::ExtractOp::create(builder, loc, lhs, width - 1, 1);
    Value one = hw::ConstantOp::create(builder, loc, APInt(width, 1));
    Value allOnes =
        hw::ConstantOp::create(builder, loc, APInt::getAllOnes(width));
    Value signedZeroDiv =
        builder.createOrFold<comb::MuxOp>(loc, sign, one, allOnes);
    return builder.createOrFold<comb::MuxOp>(loc, isZero, signedZeroDiv,
                                             result);
  }
  Value allOnes =
      hw::ConstantOp::create(builder, loc, APInt::getAllOnes(width));
  return builder.createOrFold<comb::MuxOp>(loc, isZero, allOnes, result);
}

FailureOr<Value> Materializer::materializeUnary(const BTOR2Line &line,
                                                StringRef op, Value input) {
  if (op == "not")
    return createNot(input);
  if (op == "neg") {
    APInt zero(input.getType().getIntOrFloatBitWidth(), 0);
    Value zeroValue = hw::ConstantOp::create(builder, loc, zero);
    return builder.createOrFold<comb::SubOp>(loc, zeroValue, input);
  }
  if (op == "redor")
    return createRedOr(input);
  if (op == "redand")
    return createRedAnd(input);
  if (op == "redxor")
    return createRedXor(input);
  if (op == "uext") {
    if (line.tokens.size() < 3)
      return emitLineError(line) << ": missing extension amount";
    auto amount = parseUInt(context, line.tokens[2], "extension amount");
    if (failed(amount))
      return failure();
    if (*amount == 0)
      return input;
    return comb::ConcatOp::create(
               builder, loc,
               ValueRange{
                   hw::ConstantOp::create(builder, loc, APInt(*amount, 0)),
                   input})
        .getResult();
  }
  if (op == "sext") {
    if (line.tokens.size() < 3)
      return emitLineError(line) << ": missing extension amount";
    auto amount = parseUInt(context, line.tokens[2], "extension amount");
    if (failed(amount))
      return failure();
    if (*amount == 0)
      return input;
    auto msb = comb::ExtractOp::create(
        builder, loc, input, input.getType().getIntOrFloatBitWidth() - 1, 1);
    auto ext = comb::ReplicateOp::create(builder, loc, msb, *amount);
    return comb::ConcatOp::create(builder, loc, ValueRange{ext, input})
        .getResult();
  }
  return emitLineError(line);
}

FailureOr<Value> Materializer::materializeBinary(const BTOR2Line &line,
                                                 StringRef op, Value lhs,
                                                 Value rhs) {
  if (op == "and")
    return builder.createOrFold<comb::AndOp>(loc, ValueRange{lhs, rhs}, true);
  if (op == "nand") {
    auto andValue =
        builder.createOrFold<comb::AndOp>(loc, ValueRange{lhs, rhs}, true);
    return createNot(andValue);
  }
  if (op == "or")
    return builder.createOrFold<comb::OrOp>(loc, ValueRange{lhs, rhs}, true);
  if (op == "xor")
    return builder.createOrFold<comb::XorOp>(loc, ValueRange{lhs, rhs}, true);
  if (op == "xnor") {
    auto xorValue =
        builder.createOrFold<comb::XorOp>(loc, ValueRange{lhs, rhs}, true);
    return createNot(xorValue);
  }
  if (op == "add")
    return builder.createOrFold<comb::AddOp>(loc, ValueRange{lhs, rhs}, true);
  if (op == "sub")
    return builder.createOrFold<comb::SubOp>(loc, lhs, rhs);
  if (op == "mul") {
    if (lhs.getType().isInteger(1))
      return builder.createOrFold<comb::AndOp>(loc, ValueRange{lhs, rhs}, true);
    return builder.createOrFold<comb::MulOp>(loc, ValueRange{lhs, rhs}, true);
  }
  if (op == "udiv" || op == "sdiv" || op == "urem" || op == "srem" ||
      op == "smod")
    return createBtorDivMod(op, lhs, rhs);
  if (op == "sll" || op == "srl") {
    if (lhs.getType().isInteger(1)) {
      auto shifted = createRedOr(rhs);
      if (failed(shifted))
        return failure();
      Value zero = hw::ConstantOp::create(builder, loc, APInt(1, 0));
      return builder.createOrFold<comb::MuxOp>(loc, *shifted, zero, lhs);
    }
  }
  if (op == "sll")
    return builder.createOrFold<comb::ShlOp>(loc, lhs, rhs);
  if (op == "srl")
    return builder.createOrFold<comb::ShrUOp>(loc, lhs, rhs);
  if (op == "sra") {
    if (lhs.getType().isInteger(1))
      return lhs;
    return builder.createOrFold<comb::ShrSOp>(loc, lhs, rhs);
  }
  if (op == "concat")
    return comb::ConcatOp::create(builder, loc, ValueRange{lhs, rhs})
        .getResult();

  std::optional<comb::ICmpPredicate> pred;
  if (op == "eq")
    pred = comb::ICmpPredicate::eq;
  else if (op == "neq")
    pred = comb::ICmpPredicate::ne;
  else if (op == "ult")
    pred = comb::ICmpPredicate::ult;
  else if (op == "ulte")
    pred = comb::ICmpPredicate::ule;
  else if (op == "ugt")
    pred = comb::ICmpPredicate::ugt;
  else if (op == "ugte")
    pred = comb::ICmpPredicate::uge;
  else if (op == "slt")
    pred = comb::ICmpPredicate::slt;
  else if (op == "slte")
    pred = comb::ICmpPredicate::sle;
  else if (op == "sgt")
    pred = comb::ICmpPredicate::sgt;
  else if (op == "sgte")
    pred = comb::ICmpPredicate::sge;

  if (pred)
    return createCompare(line, *pred, lhs, rhs);
  if (op == "implies") {
    auto notLhs = createNot(lhs);
    if (failed(notLhs))
      return failure();
    return builder.createOrFold<comb::OrOp>(loc, ValueRange{*notLhs, rhs},
                                            true);
  }
  return emitLineError(line);
}

FailureOr<Value> Materializer::materialize(uint64_t id) {
  if (auto value = values.lookup(id))
    return value;

  auto lineIt = problem.lines.find(id);
  if (lineIt == problem.lines.end())
    return emitError(context, "unknown BTOR2 value id " + Twine(id));
  const BTOR2Line &line = lineIt->second;
  if (!materializing.insert(id).second)
    return emitError(context, "cyclic BTOR2 expression at line " +
                                  Twine(line.sourceLine));
  llvm::scope_exit cleanup([&] { materializing.erase(id); });

  Type type = problem.types.lookup(id);
  unsigned width = getIntegerWidth(type);
  StringRef op = line.op;
  FailureOr<Value> result = failure();

  if (op == "input" || op == "state")
    return emitLineError(line) << ": unmapped declaration";
  if ((op == "zero" || op == "one" || op == "ones" || op == "constd" ||
       op == "consth" || op == "const") &&
      !width)
    return emitLineError(line) << ": constant result must be a bit-vector";
  if (op == "zero")
    result = hw::ConstantOp::create(builder, loc, APInt(width, 0)).getResult();
  else if (op == "one")
    result = hw::ConstantOp::create(builder, loc, APInt(width, 1)).getResult();
  else if (op == "ones")
    result = hw::ConstantOp::create(builder, loc, APInt::getAllOnes(width))
                 .getResult();
  else if (op == "constd" || op == "consth" || op == "const") {
    if (line.tokens.size() < 2)
      return emitLineError(line) << ": missing constant value";
    auto value = parseConstant(context, width, line.tokens[1],
                               op == "consth"  ? 16
                               : op == "const" ? 2
                                               : 10);
    if (failed(value))
      return failure();
    result = hw::ConstantOp::create(builder, loc, *value).getResult();
  } else if (op == "slice") {
    auto input = getOperand(line, 1);
    if (failed(input))
      return failure();
    if (line.tokens.size() < 4)
      return emitLineError(line) << ": missing slice bounds";
    auto high = parseUInt(context, line.tokens[2], "slice high bit");
    auto low = parseUInt(context, line.tokens[3], "slice low bit");
    if (failed(high) || failed(low))
      return failure();
    result =
        comb::ExtractOp::create(builder, loc, *input, *low, *high - *low + 1)
            .getResult();
  } else if (op == "ite") {
    auto cond = getOperand(line, 1);
    auto trueValue = getOperand(line, 2);
    auto falseValue = getOperand(line, 3);
    if (failed(cond) || failed(trueValue) || failed(falseValue))
      return failure();
    result =
        builder.createOrFold<comb::MuxOp>(loc, *cond, *trueValue, *falseValue);
  } else if (op == "read") {
    auto array = getOperand(line, 1);
    auto index = getOperand(line, 2);
    if (failed(array) || failed(index))
      return failure();
    result = hw::ArrayGetOp::create(builder, loc, *array, *index).getResult();
  } else if (op == "write") {
    auto array = getOperand(line, 1);
    auto index = getOperand(line, 2);
    auto value = getOperand(line, 3);
    if (failed(array) || failed(index) || failed(value))
      return failure();
    result = hw::ArrayInjectOp::create(builder, loc, *array, *index, *value)
                 .getResult();
  } else {
    auto lhs = getOperand(line, 1);
    if (failed(lhs))
      return failure();
    if (line.tokens.size() == 2 || op == "not" || op == "neg" ||
        op == "redor" || op == "redand" || op == "redxor" || op == "uext" ||
        op == "sext")
      result = materializeUnary(line, op, *lhs);
    else {
      auto rhs = getOperand(line, 2);
      if (failed(rhs))
        return failure();
      result = materializeBinary(line, op, *lhs, *rhs);
    }
  }

  if (failed(result))
    return failure();
  values[id] = *result;
  return *result;
}

LogicalResult
Materializer::materializeCircuit(verif::BoundedModelCheckingOp bmcOp) {
  builder.setInsertionPointToStart(&bmcOp.getCircuit().front());
  Block &circuit = bmcOp.getCircuit().front();
  unsigned argIndex = problem.states.empty() ? 0 : 1;
  for (const NodeInfo &input : problem.inputs)
    values[input.line.id] = circuit.getArgument(argIndex++);
  for (const StateInfo &state : problem.states)
    values[state.node.line.id] = circuit.getArgument(argIndex++);

  for (auto [constraint, line] : problem.constraints) {
    auto value = materializeRef(constraint);
    if (failed(value))
      return failure();
    auto label = builder.getStringAttr("constraint_" + Twine(line));
    verif::AssumeOp::create(builder, loc, *value, Value{}, label);
  }

  for (auto [bad, line] : problem.bads) {
    auto value = materializeRef(bad);
    if (failed(value))
      return failure();
    auto property = createNot(*value);
    if (failed(property))
      return failure();
    auto label = builder.getStringAttr("bad_" + Twine(line));
    verif::AssertOp::create(builder, loc, *property, Value{}, label);
  }

  for (const StateInfo &state : problem.states) {
    if (!state.initialRef || state.initialValue)
      continue;
    Value stateValue = values.lookup(state.node.line.id);
    auto initialValue = materializeRef(state.initialRef);
    if (failed(initialValue))
      return failure();
    if (stateValue.getType() != initialValue->getType())
      return emitError(context, "BTOR2 init type mismatch for state " +
                                    Twine(state.node.line.id));
    auto equal = comb::ICmpOp::create(builder, loc, comb::ICmpPredicate::eq,
                                      stateValue, *initialValue);
    auto label =
        builder.getStringAttr("init_state_" + Twine(state.node.line.id));
    auto assume = verif::AssumeOp::create(builder, loc, equal, Value{}, label);
    assume->setAttr(kInitialOnlyAttr, builder.getUnitAttr());
  }

  SmallVector<Value> nextValues;
  nextValues.reserve(problem.states.size());
  for (const StateInfo &state : problem.states) {
    if (!state.next) {
      nextValues.push_back(values.lookup(state.node.line.id));
      continue;
    }
    auto next = materializeRef(state.next);
    if (failed(next))
      return failure();
    nextValues.push_back(*next);
  }
  verif::YieldOp::create(builder, loc, nextValues);
  return success();
}

FailureOr<OwningOpRef<ModuleOp>>
circt::fraig_lec::parseBTOR2File(StringRef filename, MLIRContext &context,
                                 unsigned bound) {
  auto buffer = llvm::MemoryBuffer::getFileOrSTDIN(filename);
  if (!buffer)
    return emitError(context, "cannot open BTOR2 input '" + filename + "'");

  Problem problem;
  if (failed(parseBTOR2(buffer.get()->getBuffer(), context, problem)))
    return failure();

  Location loc = UnknownLoc::get(&context);
  OpBuilder builder(&context);
  auto module = ModuleOp::create(loc);
  builder.setInsertionPointToStart(module.getBody());

  SmallVector<Attribute> initialValues;
  initialValues.reserve(problem.states.size());
  for (const StateInfo &state : problem.states) {
    if (!state.initialValue) {
      initialValues.push_back(builder.getUnitAttr());
      continue;
    }
    initialValues.push_back(builder.getIntegerAttr(
        builder.getIntegerType(state.node.width), *state.initialValue));
  }

  auto bmcOp = verif::BoundedModelCheckingOp::create(
      builder, loc, bound, problem.states.size(),
      builder.getArrayAttr(initialValues));
  if (!problem.states.empty()) {
    bmcOp->setAttr("fraig_lec.update_regs_every_step", builder.getUnitAttr());
    SmallVector<Attribute> stateNames;
    stateNames.reserve(problem.states.size());
    for (const StateInfo &state : problem.states)
      stateNames.push_back(builder.getStringAttr(state.node.name));
    bmcOp->setAttr("fraig_lec.state_names", builder.getArrayAttr(stateNames));
  }
  if (!problem.inputs.empty()) {
    SmallVector<Attribute> inputNames;
    inputNames.reserve(problem.inputs.size());
    for (const NodeInfo &input : problem.inputs)
      inputNames.push_back(builder.getStringAttr(input.name));
    bmcOp->setAttr("fraig_lec.input_names", builder.getArrayAttr(inputNames));
  }

  {
    OpBuilder::InsertionGuard guard(builder);
    Block *init = builder.createBlock(&bmcOp.getInit());
    builder.setInsertionPointToStart(init);
    if (!problem.states.empty()) {
      auto falseValue = hw::ConstantOp::create(builder, loc, APInt(1, 0));
      auto clock = seq::ToClockOp::create(builder, loc, falseValue);
      verif::YieldOp::create(builder, loc, ValueRange{clock});
    } else {
      verif::YieldOp::create(builder, loc, ValueRange{});
    }

    Block *loop = builder.createBlock(&bmcOp.getLoop());
    builder.setInsertionPointToStart(loop);
    if (!problem.states.empty()) {
      loop->addArgument(seq::ClockType::get(&context), loc);
      auto fromClock =
          seq::FromClockOp::create(builder, loc, loop->getArgument(0));
      auto trueValue = hw::ConstantOp::create(builder, loc, APInt(1, 1));
      auto toggled = comb::XorOp::create(builder, loc, fromClock, trueValue);
      auto clock = seq::ToClockOp::create(builder, loc, toggled);
      verif::YieldOp::create(builder, loc, ValueRange{clock});
    } else {
      verif::YieldOp::create(builder, loc, ValueRange{});
    }

    SmallVector<Type> circuitArgTypes;
    if (!problem.states.empty())
      circuitArgTypes.push_back(seq::ClockType::get(&context));
    for (const NodeInfo &input : problem.inputs)
      circuitArgTypes.push_back(input.type);
    for (const StateInfo &state : problem.states)
      circuitArgTypes.push_back(state.node.type);
    builder.createBlock(&bmcOp.getCircuit(), {}, circuitArgTypes,
                        SmallVector<Location>(circuitArgTypes.size(), loc));
  }

  Materializer materializer(problem, context, loc);
  if (failed(materializer.materializeCircuit(bmcOp)))
    return failure();

  return OwningOpRef<ModuleOp>(module);
}
