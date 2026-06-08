set(LLVM_LINK_COMPONENTS
  Support
)

add_llvm_executable(circt-fraig-lec
  circt-fraig-lec.cpp
)

add_dependencies(circt-fraig-lec CIRCTSynthFormalPassesIncGen)

llvm_update_compile_flags(circt-fraig-lec)

target_include_directories(circt-fraig-lec
  PRIVATE
  ${CIRCT_EXPERIMENT_SOURCE_DIR}/lib/CIRCTSynthFormal
  ${CIRCT_EXPERIMENT_SOURCE_DIR}/include/CIRCTSynthFormal
  ${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(circt-fraig-lec
  PRIVATE
  CIRCTSynthFormal
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
