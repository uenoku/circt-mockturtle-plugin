// RUN: circt-fraig-lec --stop-after=match %s | FileCheck %s

verif.lec first {
^bb0(%clk_i: i1, %a: i8, %b: i8):
  %clk = seq.to_clock %clk_i
  %next = hw.array_create %a, %b : i8
  %regs = seq.firreg %next clock %clk {name = "regs"} : !hw.array<2xi8>
  verif.yield %regs : !hw.array<2xi8>
} second {
^bb0(%clk_i: i1, %a: i8, %b: i8):
  %clk = seq.to_clock %clk_i
  %next = hw.array_create %b, %a : i8
  %regs = seq.firreg %next clock %clk {name = "regs"} : !hw.array<2xi8>
  verif.yield %regs : !hw.array<2xi8>
}

// CHECK: hw.module @lec_miter
// CHECK-SAME: regs_lhs_state
// CHECK-SAME: regs_rhs_state
// CHECK: comb.icmp ne
// CHECK-NOT: seq.firreg
// CHECK: hw.output
