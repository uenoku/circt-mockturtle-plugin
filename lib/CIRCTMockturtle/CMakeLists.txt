add_mlir_library(CIRCTMockturtle
  CIRCTMockturtlePasses.cpp
  Emap.cpp
  MockturtlePasses.cpp
  NetworkConversion.cpp
  TranslateToCpp.cpp
  Bindings/Bind.cpp

  ADDITIONAL_HEADER_DIRS
  ${CIRCT_EXPERIMENT_SOURCE_DIR}/include/CIRCTMockturtle

  DEPENDS
  CIRCTMockturtlePassesIncGen

  LINK_LIBS PUBLIC
  CIRCTComb
  CIRCTHW
  CIRCTSynth
  MLIRIR
  MLIRPass
  MLIRSupport
  MLIRTranslateLib
  MLIRTransforms
  libabcesop
  libabcsat
  mockturtle_headers
)

set(MOCKTURTLE_COMPILE_OPTIONS
  $<$<COMPILE_LANG_AND_ID:CXX,Clang,AppleClang>:-Wno-c++11-narrowing>
  $<$<COMPILE_LANG_AND_ID:CXX,Clang,AppleClang>:-Wno-ctad-maybe-unsupported>
  $<$<COMPILE_LANG_AND_ID:CXX,GNU>:-Wno-ctad-maybe-unsupported>
)
target_compile_options(CIRCTMockturtle PRIVATE
  -Wno-unknown-pragmas
  ${MOCKTURTLE_COMPILE_OPTIONS}
)
if(TARGET obj.CIRCTMockturtle)
  target_compile_options(obj.CIRCTMockturtle PRIVATE
    -Wno-unknown-pragmas
    ${MOCKTURTLE_COMPILE_OPTIONS}
  )
endif()
set_source_files_properties(Bindings/Bind.cpp PROPERTIES
  COMPILE_FLAGS "-fexceptions -frtti -Wno-unknown-pragmas"
  SKIP_PRECOMPILE_HEADERS ON
)
