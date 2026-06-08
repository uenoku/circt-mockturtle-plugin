// RUN: circt-fraig-lec --stop-after=lower %s | FileCheck %s

hw.module private @RegXor(in %clk: !seq.clock, in %a: i8, out y: i8) {
  %r = seq.compreg %a, %clk {name = "acc"} : i8
  %y = comb.xor %r, %a : i8
  hw.output %y : i8
}

hw.module private @RegXorSwapped(in %clk: !seq.clock, in %a: i8, out y: i8) {
  %r = seq.compreg %a, %clk {name = "acc"} : i8
  %y = comb.xor %a, %r : i8
  hw.output %y : i8
}

verif.lec first {
^bb0(%clk: !seq.clock, %a: i8):
  %y = hw.instance "u" @RegXor(clk: %clk: !seq.clock, a: %a: i8) -> (y: i8)
  verif.yield %y : i8
} second {
^bb0(%clk: !seq.clock, %a: i8):
  %y = hw.instance "u" @RegXorSwapped(clk: %clk: !seq.clock, a: %a: i8) -> (y: i8)
  verif.yield %y : i8
}

// CHECK: hw.module private @RegXor
// CHECK: hw.module private @RegXorSwapped
// CHECK: hw.module @lec_miter
// CHECK-SAME: u2Facc_lhs_state
// CHECK-SAME: u2Facc_rhs_state
// CHECK: hw.instance "u" @RegXor
// CHECK-SAME: fraig_lec.path = ""
// CHECK-SAME: fraig_lec.side = "lhs"
// CHECK: hw.instance "u" @RegXorSwapped
// CHECK-SAME: fraig_lec.path = ""
// CHECK-SAME: fraig_lec.side = "rhs"
// CHECK: comb.icmp ne
