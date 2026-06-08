// RUN: circt-mockturtle-opt %s --synth-mockturtle-aig-stats | FileCheck %s
// RUN: circt-mockturtle-opt %s --synth-structural-hash | FileCheck %s --check-prefix=STRUCTURAL-HASH
// RUN: circt-opt %s --load-pass-plugin=%plugin --pass-pipeline='builtin.module(hw.module(synth-mockturtle-aig-stats))' | FileCheck %s
// UNSUPPORTED: no-circt-experiment-plugin

// CHECK-LABEL: hw.module @simple
// CHECK-SAME: mockturtle.aig_gates = 2
// STRUCTURAL-HASH-LABEL: hw.module @simple
hw.module @simple(in %a : i1, in %b : i1, in %c : i1, out out : i1) {
  %0 = synth.aig.and_inv %a, %b : i1
  %1 = synth.aig.and_inv %0, not %c : i1
  hw.output %1 : i1
}
