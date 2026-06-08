// RUN: circt-fraig-lec --stop-after=match %s | FileCheck %s

verif.lec first {
^bb0(%clk_i: i1, %a: i8):
  %clk = seq.to_clock %clk_i
  %r = seq.firreg %a clock %clk {name = "acc"} : i8
  verif.yield %r : i8
} second {
^bb0(%clk_i: i1, %a: i8):
  %clk = seq.to_clock %clk_i
  %r = seq.firreg %a clock %clk {name = "acc"} : i8
  verif.yield %r : i8
}

// CHECK: hw.module @lec_miter
// CHECK-NOT: seq.firreg
// CHECK: hw.output
