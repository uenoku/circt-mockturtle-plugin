// RUN: not circt-fraig-lec %s 2>&1 | FileCheck %s

// Not equivalent: (a + b) != (a ^ b) for some inputs.
verif.lec first {
^bb0(%a: i8, %b: i8):
  %sum = comb.add %a, %b : i8
  verif.yield %sum : i8
} second {
^bb0(%a: i8, %b: i8):
  %xor = comb.xor %a, %b : i8
  verif.yield %xor : i8
}

// CHECK: lec_miter: not proven
// CHECK: output 0: not proven
