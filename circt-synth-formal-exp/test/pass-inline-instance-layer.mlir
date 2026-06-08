// RUN: circt-fraig-lec --stop-after=inline --inline-iterations=1 %s | FileCheck %s

hw.module private @Leaf(in %clk: !seq.clock, in %a: i8, out y: i8) {
  %r = seq.compreg %a, %clk {name = "acc"} : i8
  hw.output %r : i8
}

hw.module private @Wrapper(in %clk: !seq.clock, in %a: i8, out y: i8) {
  %y = hw.instance "leaf" @Leaf(clk: %clk: !seq.clock, a: %a: i8) -> (y: i8)
  hw.output %y : i8
}

verif.lec first {
^bb0(%clk: !seq.clock, %a: i8):
  %y = hw.instance "u" @Wrapper(clk: %clk: !seq.clock, a: %a: i8) -> (y: i8)
  verif.yield %y : i8
} second {
^bb0(%clk: !seq.clock, %a: i8):
  %y = hw.instance "u" @Wrapper(clk: %clk: !seq.clock, a: %a: i8) -> (y: i8)
  verif.yield %y : i8
}

// CHECK: hw.module @lec_miter
// CHECK-NOT: hw.instance "u" @Wrapper
// CHECK: hw.instance "leaf" @Leaf
// CHECK-SAME: fraig_lec.path = "u"
// CHECK-SAME: fraig_lec.side = "lhs"
// CHECK: hw.instance "leaf" @Leaf
// CHECK-SAME: fraig_lec.path = "u"
// CHECK-SAME: fraig_lec.side = "rhs"
