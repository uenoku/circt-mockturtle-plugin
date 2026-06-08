set(LLVM_LINK_COMPONENTS
  Support
)

add_llvm_executable(circt-experiment-opt
  circt-experiment-opt.cpp

  DEPENDS
  SUPPORT_PLUGINS
)

llvm_update_compile_flags(circt-experiment-opt)

target_link_libraries(circt-experiment-opt
  PRIVATE
  CIRCTMockturtle
  CIRCTSynthFormal
  CIRCTComb
  CIRCTHW
  CIRCTSynth
  CIRCTSynthTransforms
  MLIRIR
  MLIROptLib
  MLIRParser
  MLIRSupport
  MLIRTransforms
)

export_executable_symbols_for_plugins(circt-experiment-opt)
