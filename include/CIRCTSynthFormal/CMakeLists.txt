set(LLVM_TARGET_DEFINITIONS CIRCTSynthFormalPasses.td)
mlir_tablegen(CIRCTSynthFormalPasses.h.inc -gen-pass-decls -name CIRCTFRAIGLEC)
add_public_tablegen_target(CIRCTSynthFormalPassesIncGen)
