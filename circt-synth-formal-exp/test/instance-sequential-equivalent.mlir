// RUN: circt-fraig-lec %s | FileCheck %s

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

// Equivalent after flattening the instance and matching register "u/acc".
verif.lec first {
^bb0(%clk: !seq.clock, %a: i8):
  %y = hw.instance "u" @RegXor(clk: %clk: !seq.clock, a: %a: i8) -> (y: i8)
  verif.yield %y : i8
} second {
^bb0(%clk: !seq.clock, %a: i8):
  %y = hw.instance "u" @RegXorSwapped(clk: %clk: !seq.clock, a: %a: i8) -> (y: i8)
  verif.yield %y : i8
}

// CHECK: lec_miter: equivalent
// CHECK: output 0: proven
// CHECK: register u/acc next-state: proven
