// RUN: circt-mockturtle-opt %s --pass-pipeline='builtin.module(hw.module(synth-mockturtle-aig-resubstitution{max-pis=6 max-divisors=64 max-inserts=1 preserve-depth=true}))' | FileCheck %s --check-prefix=AIG
// RUN: circt-mockturtle-opt %s --pass-pipeline='builtin.module(hw.module(synth-mockturtle-mig-resubstitution2{max-pis=6 max-divisors=64 max-inserts=1 use-dont-cares=false window-size=8}))' | FileCheck %s --check-prefix=MIG
// RUN: circt-mockturtle-opt %s --pass-pipeline='builtin.module(hw.module(synth-mockturtle-xag-resubstitution{max-pis=6 max-divisors=64 max-inserts=1}))' | FileCheck %s --check-prefix=XAG
// RUN: circt-mockturtle-opt %s --pass-pipeline='builtin.module(hw.module(synth-mockturtle-xmg-resubstitution{max-pis=6 max-divisors=64 max-inserts=1}))' | FileCheck %s --check-prefix=XMG
// UNSUPPORTED: no-circt-mockturtle-plugin

// AIG-LABEL: hw.module @aig_const
// AIG: %false = hw.constant false
// AIG: hw.output %false : i1
hw.module @aig_const(in %a : i1, out b : i1) {
  %false = hw.constant false
  %0 = synth.aig.and_inv %a, %false : i1
  %1 = synth.aig.and_inv %0, %a : i1
  hw.output %0 : i1
}

// MIG-LABEL: hw.module @mig_const
// MIG: hw.output %a : i1
hw.module @mig_const(in %a : i1, out b : i1) {
  %false = hw.constant false
  %true = hw.constant true
  %0 = synth.majority %true, %a, %false : i1
  hw.output %0 : i1
}

// XAG-LABEL: hw.module @xag_const
// XAG: hw.output %a : i1
hw.module @xag_const(in %a : i1, out b : i1) {
  %false = hw.constant false
  %0 = synth.xor_inv %a, %false : i1
  hw.output %0 : i1
}

// XMG-LABEL: hw.module @xmg_const
// XMG: hw.output %a : i1
hw.module @xmg_const(in %a : i1, out b : i1) {
  %false = hw.constant false
  %true = hw.constant true
  %0 = synth.majority %true, %a, %false : i1
  %1 = synth.xor_inv %0, %false : i1
  hw.output %1 : i1
}
