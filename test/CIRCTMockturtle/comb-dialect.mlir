// RUN: circt-experiment-opt %s --synth-mockturtle-aig-stats | FileCheck %s

// CHECK-LABEL: hw.module @comb_extract
// CHECK-SAME: mockturtle.aig_gates = 1
hw.module @comb_extract(in %a : i1, in %c : i16, out out : i1) {
  %0 = comb.extract %c from 0 : (i16) -> i1
  %1 = synth.aig.and_inv %a, %0 : i1
  hw.output %1 : i1
}
