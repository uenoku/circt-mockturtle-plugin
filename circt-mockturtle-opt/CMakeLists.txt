set(LLVM_LINK_COMPONENTS
  Support
)

add_llvm_executable(circt-mockturtle-opt
  circt-mockturtle-opt.cpp

  DEPENDS
  SUPPORT_PLUGINS
)

llvm_update_compile_flags(circt-mockturtle-opt)

target_link_libraries(circt-mockturtle-opt
  PRIVATE
  CIRCTMockturtle
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

export_executable_symbols_for_plugins(circt-mockturtle-opt)
