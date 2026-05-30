add_mlir_library(CIRCTMockturtle
  CIRCTMockturtlePasses.cpp

  ADDITIONAL_HEADER_DIRS
  ${CIRCT_MOCKTURTLE_SOURCE_DIR}/include/CIRCTMockturtle

  DEPENDS
  CIRCTMockturtlePassesIncGen

  LINK_LIBS PUBLIC
  CIRCTHW
  CIRCTSynth
  MLIRIR
  MLIRPass
  MLIRSupport
  mockturtle_headers
)

target_compile_options(CIRCTMockturtle PRIVATE -Wno-unknown-pragmas)
