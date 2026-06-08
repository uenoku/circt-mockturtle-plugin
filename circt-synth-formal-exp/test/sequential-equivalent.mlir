// RUN: circt-fraig-lec %s | FileCheck %s

// Equivalent with a matched register cut-point. The register state is matched
// by name and the next-state inputs are identical.
verif.lec first {
^bb0(%clk: !seq.clock, %a: i8):
  %r = seq.compreg %a, %clk {name = "acc"} : i8
  %y = comb.xor %r, %a : i8
  verif.yield %y : i8
} second {
^bb0(%clk: !seq.clock, %a: i8):
  %r = seq.compreg %a, %clk {name = "acc"} : i8
  %y = comb.xor %a, %r : i8
  verif.yield %y : i8
}

// CHECK: lec_miter: equivalent
// CHECK: output 0: proven
// CHECK: register acc next-state: proven
