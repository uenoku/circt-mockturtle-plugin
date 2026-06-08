; RUN: circt-experiment-translate --btor2-to-mlir --btor2-bound=1 %S/Inputs/comb.btor2 | FileCheck %s

; Verify that the BTOR2-to-MLIR translation pipeline produces verif.bmc.
; CHECK: verif.bmc
; CHECK-SAME: bound 1 num_regs 0 initial_values []
; CHECK-SAME: fraig_lec.input_names = ["a"]
; CHECK: circuit {
; CHECK: ^bb0(%{{.*}}: i1):
; CHECK: verif.assert {{.*}} label "bad_4" : i1
