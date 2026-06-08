add_mlir_library(CIRCTSynthFormal
  BMCPDR.cpp
  BTOR2Importer.cpp
  Counterexample.cpp
  InlineInstanceLayer.cpp
  LowerBMCToMiter.cpp
  LowerLECToMiter.cpp
  MatchRegisters.cpp
  MiterUtils.cpp
  PruneNonMiterModules.cpp
  ResolveChoices.cpp
  Translations.cpp

  ADDITIONAL_HEADER_DIRS
  ${CIRCT_EXPERIMENT_SOURCE_DIR}/include/CIRCTSynthFormal

  DEPENDS
  CIRCTSynthFormalPassesIncGen

  LINK_LIBS PUBLIC
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
  MLIRPass
  MLIRParser
  MLIRSupport
  MLIRTransforms
)
