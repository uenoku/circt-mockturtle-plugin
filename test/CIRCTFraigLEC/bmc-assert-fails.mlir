// RUN: not circt-fraig-lec %s 2>&1 | FileCheck %s

verif.bmc bound 2 num_regs 0 initial_values [] init {
} loop {
} circuit {
^bb0(%a: i8):
  %false = hw.constant false
  verif.assert %false label "always_false" : i1
  verif.yield
}

// CHECK: bmc_miter: not proven
// CHECK: step 0 assertion always_false: not proven
// CHECK: step 1 assertion always_false: not proven

