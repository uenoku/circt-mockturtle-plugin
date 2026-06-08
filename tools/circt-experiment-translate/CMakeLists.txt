set(LLVM_LINK_COMPONENTS
  Support
)

add_llvm_executable(circt-experiment-translate
  circt-experiment-translate.cpp
)

llvm_update_compile_flags(circt-experiment-translate)

target_link_libraries(circt-experiment-translate
  PRIVATE
  CIRCTMockturtle
  CIRCTSynthFormal
  CIRCTComb
  CIRCTHW
  CIRCTSynth
  MLIRIR
  MLIRParser
  MLIRSupport
  MLIRTranslateLib
)
