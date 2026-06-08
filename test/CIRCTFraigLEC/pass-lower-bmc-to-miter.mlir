// RUN: circt-fraig-lec --stop-after=lower %s | FileCheck %s

verif.bmc bound 2 num_regs 0 initial_values [] init {
} loop {
} circuit {
^bb0(%a: i8):
  %eq = comb.icmp eq %a, %a : i8
  verif.assert %eq label "self_eq" : i1
  verif.yield
}

// CHECK-LABEL: hw.module @bmc_miter(
// CHECK-SAME: in %step0_in0 : i8
// CHECK-SAME: in %step1_in0 : i8
// CHECK-SAME: out fail : i1
// CHECK-SAME: out check0 : i1
// CHECK-SAME: out check1 : i1
// CHECK-SAME: fraig_lec.check_names = ["step 0 assertion self_eq", "step 1 assertion self_eq"]
// CHECK-SAME: fraig_lec.miter
// CHECK-SAME: fraig_lec.miter_kind = "bmc"
