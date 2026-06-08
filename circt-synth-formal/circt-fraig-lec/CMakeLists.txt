set(LLVM_LINK_COMPONENTS
  Support
)

set(LLVM_TARGET_DEFINITIONS Passes.td)
mlir_tablegen(Passes.h.inc -gen-pass-decls -name CIRCTFRAIGLEC)
add_public_tablegen_target(CIRCTFRAIGLECPassesIncGen)

add_llvm_executable(circt-fraig-lec
  BMCPDR.cpp
  BTOR2Importer.cpp
  circt-fraig-lec.cpp
  Counterexample.cpp
  InlineInstanceLayer.cpp
  LowerBMCToMiter.cpp
  LowerLECToMiter.cpp
  MatchRegisters.cpp
  MiterUtils.cpp
  PruneNonMiterModules.cpp
  ResolveChoices.cpp
)

add_dependencies(circt-fraig-lec CIRCTFRAIGLECPassesIncGen)

llvm_update_compile_flags(circt-fraig-lec)

target_include_directories(circt-fraig-lec
  PRIVATE
  ${CMAKE_CURRENT_BINARY_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(circt-fraig-lec
  PRIVATE
  CIRCTComb
  CIRCTCombToSynth
  CIRCTHW
  CIRCTHWTransforms
  CIRCTSeq
  CIRCTSupport
  CIRCTSynth
  CIRCTSynthTransforms
  CIRCTVerif
  LLVMSupport
  MLIRIR
  MLIRParser
  MLIRPass
  MLIRSupport
  MLIRTransforms
)
