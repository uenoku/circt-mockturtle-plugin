add_llvm_library(CIRCTMockturtlePlugin
  MODULE BUILDTREE_ONLY
  circt-mockturtle-plugin.cpp

  DEPENDS
  CIRCTMockturtle
  PLUGIN_TOOL
  circt-opt

  LINK_LIBS
  CIRCTMockturtle
)

target_include_directories(CIRCTMockturtlePlugin
  PRIVATE
  "${CIRCT_EXPERIMENT_BINARY_DIR}/include"
)
