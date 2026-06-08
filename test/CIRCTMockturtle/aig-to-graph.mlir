// RUN: circt-mockturtle-opt %s --pass-pipeline='builtin.module(hw.module(synth-mockturtle-aig-to-xag{verbose=false}))' | FileCheck %s --check-prefix=XAG
// RUN: circt-mockturtle-opt %s --pass-pipeline='builtin.module(hw.module(synth-mockturtle-aig-to-mig{use-multiple=true verbose=false}))' | FileCheck %s --check-prefix=MIG
// RUN: circt-mockturtle-opt %s --pass-pipeline='builtin.module(hw.module(synth-mockturtle-aig-to-xmg{verbose=false}))' | FileCheck %s --check-prefix=XMG
// UNSUPPORTED: no-circt-experiment-plugin

// XAG-LABEL: hw.module @aig_to_graph
// XAG: synth.aig.and_inv
// XAG-NOT: synth.majority
// XAG: hw.output
// MIG-LABEL: hw.module @aig_to_graph
// MIG: hw.constant false
// MIG: synth.majority
// MIG-NOT: synth.aig.and_inv
// MIG: hw.output
// XMG-LABEL: hw.module @aig_to_graph
// XMG: hw.constant false
// XMG: synth.majority
// XMG-NOT: synth.aig.and_inv
// XMG: hw.output
hw.module @aig_to_graph(in %a : i1, in %b : i1, in %c : i1, out out : i1) {
  %0 = synth.aig.and_inv %a, %b : i1
  %1 = synth.aig.and_inv not %0, %c : i1
  hw.output %1 : i1
}
