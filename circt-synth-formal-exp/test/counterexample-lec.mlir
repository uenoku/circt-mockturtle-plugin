// RUN: not circt-fraig-lec --counterexample %s 2>&1 | FileCheck %s

verif.lec first {
^bb0(%a: i2, %b: i2):
  %sum = comb.add %a, %b : i2
  verif.yield %sum : i2
} second {
^bb0(%a: i2, %b: i2):
  %xor = comb.xor %a, %b : i2
  verif.yield %xor : i2
}

// CHECK: lec_miter: not proven
// CHECK: output 0: not proven
// CHECK: counterexample:
// CHECK: in0 = 0x
// CHECK: in1 = 0x
