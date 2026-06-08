// RUN: not circt-fraig-lec %s 2>&1 | FileCheck %s

verif.lec first {
^bb0(%a: i8):
  verif.yield %a : i8
} second {
^bb0(%a: i8):
  %c1_i8 = hw.constant 1 : i8
  %b = comb.xor %a, %c1_i8 : i8
  verif.yield %b : i8
}

// CHECK: lec_miter: not proven
// CHECK: output 0: not proven
