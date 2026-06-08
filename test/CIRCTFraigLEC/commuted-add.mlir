// RUN: circt-fraig-lec %s | FileCheck %s

// Equivalent: (a + b) == (b + a)
verif.lec first {
^bb0(%a: i8, %b: i8):
  %sum = comb.add %a, %b : i8
  verif.yield %sum : i8
} second {
^bb0(%a: i8, %b: i8):
  %sum = comb.add %b, %a : i8
  verif.yield %sum : i8
}

// CHECK: lec_miter: equivalent
// CHECK: output 0: proven
