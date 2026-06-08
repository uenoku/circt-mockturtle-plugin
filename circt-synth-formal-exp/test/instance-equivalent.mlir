// RUN: circt-fraig-lec %s | FileCheck %s

hw.module private @Add(in %a: i8, in %b: i8, out y: i8) {
  %sum = comb.add %a, %b : i8
  hw.output %sum : i8
}

hw.module private @AddSwapped(in %a: i8, in %b: i8, out y: i8) {
  %sum = comb.add %b, %a : i8
  hw.output %sum : i8
}

// Equivalent after miter-local instance flattening.
verif.lec first {
^bb0(%a: i8, %b: i8):
  %y = hw.instance "u" @Add(a: %a: i8, b: %b: i8) -> (y: i8)
  verif.yield %y : i8
} second {
^bb0(%a: i8, %b: i8):
  %y = hw.instance "u" @AddSwapped(a: %a: i8, b: %b: i8) -> (y: i8)
  verif.yield %y : i8
}

// CHECK: lec_miter: equivalent
// CHECK: output 0: proven
