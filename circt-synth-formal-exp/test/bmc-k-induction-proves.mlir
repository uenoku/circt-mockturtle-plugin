// RUN: circt-fraig-lec --bmc-k-induction %s | FileCheck %s

verif.bmc bound 1 num_regs 1 initial_values [0 : i2] init {
  %false = hw.constant false
  %clk = seq.to_clock %false
  verif.yield %clk : !seq.clock
} loop {
^bb0(%clk: !seq.clock):
  %from_clk = seq.from_clock %clk
  %true = hw.constant true
  %next_clk_value = comb.xor %from_clk, %true : i1
  %next_clk = seq.to_clock %next_clk_value
  verif.yield %next_clk : !seq.clock
} circuit {
^bb0(%clk: !seq.clock, %state: i2):
  %zero = hw.constant 0 : i2
  %state_is_zero = comb.icmp eq %state, %zero : i2
  verif.assert %state_is_zero label "state_zero" : i1
  verif.yield %state : i2
}

// CHECK: bmc_miter: safe within bound
// CHECK: step 0 assertion state_zero: proven
// CHECK: bmc_induction_miter: inductive step proven
// CHECK: step 1 assertion state_zero: proven
