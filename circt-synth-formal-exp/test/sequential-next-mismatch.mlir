// RUN: not circt-fraig-lec %s 2>&1 | FileCheck %s

// Not equivalent: the visible outputs match under the register cut-point, but
// the matched register has different next-state logic.
verif.lec first {
^bb0(%clk: !seq.clock, %a: i8):
  %r = seq.compreg %a, %clk {name = "acc"} : i8
  verif.yield %r : i8
} second {
^bb0(%clk: !seq.clock, %a: i8):
  %c1 = hw.constant 1 : i8
  %next = comb.add %a, %c1 : i8
  %r = seq.compreg %next, %clk {name = "acc"} : i8
  verif.yield %r : i8
}

// CHECK: lec_miter: not proven
// CHECK: output 0: proven
// CHECK: register acc next-state: not proven
