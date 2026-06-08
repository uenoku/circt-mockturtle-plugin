// RUN: circt-fraig-lec --stop-after=match %s | FileCheck %s

verif.lec first {
^bb0(%clk: !seq.clock, %a: i8):
  %r = seq.compreg %a, %clk {name = "acc"} : i8
  verif.yield %r : i8
} second {
^bb0(%clk: !seq.clock, %a: i8):
  %r = seq.compreg %a, %clk {name = "acc"} : i8
  verif.yield %a : i8
}

// CHECK: hw.module @lec_miter
// CHECK-SAME: acc_lhs_state
// CHECK-SAME: acc_rhs_state
// CHECK: synth.choice
// CHECK: comb.icmp ne
// CHECK: hw.output
// CHECK-NOT: seq.compreg
