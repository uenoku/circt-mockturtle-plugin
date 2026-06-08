// RUN: not circt-fraig-lec %s 2>&1 | FileCheck %s

verif.lec first {
^bb0(%clk: !seq.clock, %a: i8):
  %r = seq.compreg %a, %clk {name = "acc"} : i8
  verif.yield %r : i8
} second {
^bb0(%clk: !seq.clock, %a: i8):
  %c1_i8 = hw.constant 1 : i8
  %next = comb.add %a, %c1_i8 : i8
  %r = seq.compreg %next, %clk {name = "acc"} : i8
  verif.yield %r : i8
}

// CHECK: lec_miter: not proven
// CHECK: output 0: proven
// CHECK: register acc next-state: not proven
