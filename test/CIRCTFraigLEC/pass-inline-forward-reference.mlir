// RUN: circt-fraig-lec --stop-after=inline --inline-iterations=1 %s | FileCheck %s

hw.module private @Forward(in %a: i8, out y: i8) {
  %y = comb.xor %late, %a : i8
  %late = comb.xor %a, %a : i8
  hw.output %y : i8
}

verif.lec first {
^bb0(%a: i8):
  %y = hw.instance "dut" @Forward(a: %a: i8) -> (y: i8)
  verif.yield %y : i8
} second {
^bb0(%a: i8):
  %y = hw.instance "dut" @Forward(a: %a: i8) -> (y: i8)
  verif.yield %y : i8
}

// CHECK: hw.module private @Forward
// CHECK: hw.module @lec_miter
// CHECK-NOT: hw.instance "dut"
// CHECK: hw.output
