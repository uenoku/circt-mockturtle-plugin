add_llvm_library(CIRCTExperimentPlugin
  MODULE BUILDTREE_ONLY
  circt-experiment-plugin.cpp

  DEPENDS
  CIRCTMockturtle
  PLUGIN_TOOL
  circt-opt

  LINK_LIBS
  CIRCTMockturtle
)

target_include_directories(CIRCTExperimentPlugin
  PRIVATE
  "${CIRCT_EXPERIMENT_BINARY_DIR}/include"
)
