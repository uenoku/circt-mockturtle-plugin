// RUN: circt-mockturtle-opt %s --pass-pipeline='builtin.module(hw.module(synth-mockturtle-refactor{max-pis=4 allow-zero-gain=true use-reconvergence-cut=false use-dont-cares=false progress=false verbose=false}))' | FileCheck %s --check-prefix=AIG
// RUN: circt-mockturtle-opt %s --pass-pipeline='builtin.module(hw.module(synth-mockturtle-functional-reduction{max-iterations=1 max-tfi-nodes=16 skip-fanout-limit=8 conflict-limit=10 max-clauses=64 num-patterns=32 max-patterns=64 progress=false verbose=false}))' | FileCheck %s --check-prefix=AIG
// RUN: circt-mockturtle-opt %s --pass-pipeline='builtin.module(hw.module(synth-mockturtle-sop-balancing{cut-size=4 cut-limit=8 area-oriented-mapping=false required-delay=0 relax-required=0 recompute-cuts=true area-share-rounds=1 area-flow-rounds=1 ela-rounds=1 edge-optimization=true cut-expansion=true remove-dominated-cuts=true cost-cache-vars=3 verbose=false}))' | FileCheck %s --check-prefix=AIG
// RUN: circt-mockturtle-opt %s --pass-pipeline='builtin.module(hw.module(synth-mockturtle-aig-balancing{minimize-levels=false fast-mode=false}))' | FileCheck %s --check-prefix=AIG
// RUN: circt-mockturtle-opt %s --pass-pipeline='builtin.module(hw.module(synth-mockturtle-aig-resubstitution{max-pis=8 max-divisors=16 max-inserts=1 skip-fanout-limit-for-roots=32 skip-fanout-limit-for-divisors=16 use-dont-cares=false preserve-depth=false max-clauses=64 conflict-limit=64 random-seed=1 odc-levels=0 max-trials=8 max-divisors-k=8 progress=false verbose=false}))' | FileCheck %s --check-prefix=AIG
// RUN: circt-mockturtle-opt %s --pass-pipeline='builtin.module(hw.module(synth-mockturtle-aig-resubstitution2{max-pis=8 max-divisors=16 max-inserts=1 skip-fanout-limit-for-roots=32 skip-fanout-limit-for-divisors=16 use-dont-cares=false preserve-depth=false max-clauses=64 conflict-limit=64 random-seed=1 odc-levels=0 max-trials=8 max-divisors-k=8 progress=false verbose=false}))' | FileCheck %s --check-prefix=AIG
// RUN: circt-mockturtle-opt %s --pass-pipeline='builtin.module(hw.module(synth-mockturtle-xag-balancing{minimize-levels=false fast-mode=false}))' | FileCheck %s --check-prefix=XAG
// RUN: circt-mockturtle-opt %s --pass-pipeline='builtin.module(hw.module(synth-mockturtle-xag-resubstitution{max-pis=8 max-divisors=16 max-inserts=1 skip-fanout-limit-for-roots=32 skip-fanout-limit-for-divisors=16 use-dont-cares=false preserve-depth=false max-clauses=64 conflict-limit=64 random-seed=1 odc-levels=0 max-trials=8 max-divisors-k=8 progress=false verbose=false}))' | FileCheck %s --check-prefix=XAG
// RUN: circt-mockturtle-opt %s --pass-pipeline='builtin.module(hw.module(synth-mockturtle-xag-algebraic-rewrite-depth{strategy=dfs overhead=1.5 allow-area-increase=true}))' | FileCheck %s --check-prefix=XAG
// RUN: circt-mockturtle-opt %s --pass-pipeline='builtin.module(hw.module(synth-mockturtle-mig-algebraic-rewrite-depth{strategy=dfs overhead=1.5 allow-area-increase=true}))' | FileCheck %s --check-prefix=MIG
// RUN: circt-mockturtle-opt %s --pass-pipeline='builtin.module(hw.module(synth-mockturtle-mig-resubstitution{max-pis=8 max-divisors=16 max-inserts=1 skip-fanout-limit-for-roots=32 skip-fanout-limit-for-divisors=16 use-dont-cares=false preserve-depth=false max-clauses=64 conflict-limit=64 random-seed=1 odc-levels=0 max-trials=8 max-divisors-k=8 progress=false verbose=false}))' | FileCheck %s --check-prefix=MIG
// RUN: circt-mockturtle-opt %s --pass-pipeline='builtin.module(hw.module(synth-mockturtle-mig-resubstitution2{max-pis=8 max-divisors=16 max-inserts=1 skip-fanout-limit-for-roots=32 skip-fanout-limit-for-divisors=16 use-dont-cares=false preserve-depth=false max-clauses=64 conflict-limit=64 random-seed=1 odc-levels=0 max-trials=8 max-divisors-k=8 progress=false verbose=false}))' | FileCheck %s --check-prefix=MIG
// RUN: circt-mockturtle-opt %s --pass-pipeline='builtin.module(hw.module(synth-mockturtle-mig-inv-propagation))' | FileCheck %s --check-prefix=MIG
// RUN: circt-mockturtle-opt %s --pass-pipeline='builtin.module(hw.module(synth-mockturtle-mig-inv-optimization))' | FileCheck %s --check-prefix=MIG
// RUN: circt-mockturtle-opt %s --pass-pipeline='builtin.module(hw.module(synth-mockturtle-xmg-resubstitution{max-pis=8 max-divisors=16 max-inserts=1 skip-fanout-limit-for-roots=32 skip-fanout-limit-for-divisors=16 use-dont-cares=false preserve-depth=false max-clauses=64 conflict-limit=64 random-seed=1 odc-levels=0 max-trials=8 max-divisors-k=8 progress=false verbose=false}))' | FileCheck %s --check-prefix=XMG
// RUN: circt-mockturtle-opt %s --pass-pipeline='builtin.module(hw.module(synth-mockturtle-xmg-algebraic-rewrite-depth{strategy=dfs overhead=1.5 allow-area-increase=true}))' | FileCheck %s --check-prefix=XMG
// RUN: circt-opt %s --load-pass-plugin=%plugin --pass-pipeline='builtin.module(hw.module(synth-mockturtle-aig-balancing{minimize-levels=true fast-mode=true}))' | FileCheck %s --check-prefix=AIG
// UNSUPPORTED: no-circt-experiment-plugin

// AIG-LABEL: hw.module @aig
// AIG: synth.aig.and_inv
// AIG: hw.output
hw.module @aig(in %a : i1, in %b : i1, in %c : i1, out out : i1) {
  %0 = synth.aig.and_inv %a, %b : i1
  %1 = synth.aig.and_inv %0, not %c : i1
  hw.output %1 : i1
}

// XAG-LABEL: hw.module @xag
// XAG: synth.aig.and_inv
// XAG: synth.xor_inv
// XAG: hw.output
hw.module @xag(in %a : i1, in %b : i1, in %c : i1, out out : i1) {
  %0 = synth.aig.and_inv %a, %b : i1
  %1 = synth.xor_inv %0, %c : i1
  hw.output %1 : i1
}

// MIG-LABEL: hw.module @mig
// MIG: synth.majority
// MIG: hw.output
hw.module @mig(in %a : i1, in %b : i1, in %c : i1, out out : i1) {
  %0 = synth.majority %a, %b, %c : i1
  hw.output %0 : i1
}

// XMG-LABEL: hw.module @xmg
// XMG: synth.majority
// XMG: synth.xor_inv
// XMG: hw.output
hw.module @xmg(in %a : i1, in %b : i1, in %c : i1, out out : i1) {
  %0 = synth.majority %a, %b, %c : i1
  %1 = synth.xor_inv %0, %a : i1
  hw.output %1 : i1
}
