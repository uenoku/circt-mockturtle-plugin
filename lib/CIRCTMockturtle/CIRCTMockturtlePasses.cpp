//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CIRCTMockturtle/CIRCTMockturtlePasses.h"
#include "circt/Dialect/HW/HWOps.h"
#include "circt/Dialect/Synth/SynthOps.h"
#include "mlir/IR/Builders.h"
#include "mockturtle/networks/aig.hpp"
#include "llvm/ADT/DenseMap.h"

namespace circt {
namespace mockturtle_plugin {
#define GEN_PASS_DEF_MOCKTURTLEAIGSTATS
#include "CIRCTMockturtle/CIRCTMockturtlePasses.h.inc"

namespace {

struct MockturtleAIGStats
    : public impl::MockturtleAIGStatsBase<MockturtleAIGStats> {
  using impl::MockturtleAIGStatsBase<
      MockturtleAIGStats>::MockturtleAIGStatsBase;

  void runOnOperation() override {
    using Signal = mockturtle::aig_network::signal;

    mockturtle::aig_network network;
    llvm::DenseMap<Value, Signal> signals;

    auto getSignal = [&](Value value) -> Signal {
      auto [it, inserted] = signals.try_emplace(value, network.create_pi());
      return it->second;
    };

    getOperation().walk([&](synth::aig::AndInverterOp op) {
      if (op.getNumOperands() == 0)
        return;

      auto applyInversion = [&](Signal signal, bool inverted) {
        return inverted ? network.create_not(signal) : signal;
      };

      Signal result =
          applyInversion(getSignal(op.getOperand(0)), op.isInverted(0));
      for (unsigned i = 1, e = op.getNumOperands(); i != e; ++i) {
        Signal rhs =
            applyInversion(getSignal(op.getOperand(i)), op.isInverted(i));
        result = network.create_and(result, rhs);
      }
      signals[op.getResult()] = result;
    });

    if (annotate) {
      OpBuilder builder(getOperation());
      getOperation()->setAttr("mockturtle.aig_gates",
                              builder.getI64IntegerAttr(network.num_gates()));
    }
  }
};

} // namespace
} // namespace mockturtle_plugin
} // namespace circt
