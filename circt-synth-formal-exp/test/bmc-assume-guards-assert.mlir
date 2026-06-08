// RUN: circt-fraig-lec %s | FileCheck %s

verif.bmc bound 2 num_regs 0 initial_values [] init {
} loop {
} circuit {
^bb0(%a: i8):
  %false = hw.constant false
  verif.assume %false label "impossible" : i1
  verif.assert %false label "guarded_false" : i1
  verif.yield
}

// CHECK: bmc_miter: safe within bound
// CHECK: step 0 assertion guarded_false: proven
// CHECK: step 1 assertion guarded_false: proven
