// RUN: circt-mockturtle-opt %s --pass-pipeline='builtin.module(hw.module(synth-mockturtle-refactor))' | FileCheck %s --check-prefix=AIG
// RUN: circt-mockturtle-opt %s --pass-pipeline='builtin.module(hw.module(synth-mockturtle-functional-reduction))' | FileCheck %s --check-prefix=AIG
// RUN: circt-mockturtle-opt %s --pass-pipeline='builtin.module(hw.module(synth-mockturtle-sop-balancing))' | FileCheck %s --check-prefix=AIG
// RUN: circt-mockturtle-opt %s --pass-pipeline='builtin.module(hw.module(synth-mockturtle-mig-algebraic-rewrite-depth))' | FileCheck %s --check-prefix=MIG
// RUN: circt-opt %s --load-pass-plugin=%plugin --pass-pipeline='builtin.module(hw.module(synth-mockturtle-refactor))' | FileCheck %s --check-prefix=AIG
// UNSUPPORTED: no-circt-mockturtle-plugin

// AIG-LABEL: hw.module @aig
// AIG: synth.aig.and_inv
// AIG: hw.output
hw.module @aig(in %a : i1, in %b : i1, in %c : i1, out out : i1) {
  %0 = synth.aig.and_inv %a, %b : i1
  %1 = synth.aig.and_inv %0, not %c : i1
  hw.output %1 : i1
}

// MIG-LABEL: hw.module @mig
// MIG: synth.majority
// MIG: hw.output
hw.module @mig(in %a : i1, in %b : i1, in %c : i1, out out : i1) {
  %0 = synth.majority %a, %b, %c : i1
  hw.output %0 : i1
}
