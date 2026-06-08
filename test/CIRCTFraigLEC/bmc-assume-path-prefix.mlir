// RUN: circt-fraig-lec %s | FileCheck %s

verif.bmc bound 2 num_regs 1 initial_values [false] attributes {ignore_asserts_until = 1 : i64} init {
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
^bb0(%clk: !seq.clock, %state: i1):
  %false = hw.constant false
  %true = hw.constant true
  verif.assume %state label "path_prefix" : i1
  verif.assert %false label "guarded_by_previous_assume" : i1
  verif.yield %true : i1
}

// CHECK: bmc_miter: safe within bound
// CHECK: step 1 assertion guarded_by_previous_assume: proven
