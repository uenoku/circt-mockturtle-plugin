add_mlir_library(CIRCTQuery
  Json.cpp
  Mcp.cpp
  Model.cpp
  Service.cpp
  Types.cpp

  ADDITIONAL_HEADER_DIRS
  ${CIRCT_EXPERIMENT_SOURCE_DIR}/include/CIRCTQuery

  LINK_LIBS PUBLIC
  CIRCTComb
  CIRCTHW
  CIRCTSeq
  CIRCTSV
  CIRCTSynth
  MLIRBytecodeReader
  MLIRIR
  MLIRParser
  MLIRSupport
)
