set(LLVM_LINK_COMPONENTS
  Support
)

add_llvm_executable(circt-mockturtle-translate
  circt-mockturtle-translate.cpp
)

llvm_update_compile_flags(circt-mockturtle-translate)

target_link_libraries(circt-mockturtle-translate
  PRIVATE
  CIRCTMockturtle
  CIRCTComb
  CIRCTHW
  CIRCTSynth
  MLIRIR
  MLIRParser
  MLIRSupport
  MLIRTranslateLib
)
