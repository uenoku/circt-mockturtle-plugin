// RUN: not circt-fraig-lec %s 2>&1 | FileCheck %s

hw.module private @RegState(in %clk: !seq.clock, in %a: i8, out y: i8) {
  %r = seq.compreg %a, %clk {name = "acc"} : i8
  hw.output %r : i8
}

hw.module private @RegStateNextPlusOne(in %clk: !seq.clock, in %a: i8, out y: i8) {
  %c1 = hw.constant 1 : i8
  %next = comb.add %a, %c1 : i8
  %r = seq.compreg %next, %clk {name = "acc"} : i8
  hw.output %r : i8
}

// Not equivalent: the flattened visible outputs match under the "u/acc"
// state cut-point, but the matched register next-state logic differs.
verif.lec first {
^bb0(%clk: !seq.clock, %a: i8):
  %y = hw.instance "u" @RegState(clk: %clk: !seq.clock, a: %a: i8) -> (y: i8)
  verif.yield %y : i8
} second {
^bb0(%clk: !seq.clock, %a: i8):
  %y = hw.instance "u" @RegStateNextPlusOne(clk: %clk: !seq.clock, a: %a: i8) -> (y: i8)
  verif.yield %y : i8
}

// CHECK: lec_miter: not proven
// CHECK: output 0: proven
// CHECK: register u/acc next-state: not proven
