configure_lit_site_cfg(
  ${CMAKE_CURRENT_SOURCE_DIR}/lit.site.cfg.py.in
  ${CMAKE_CURRENT_BINARY_DIR}/lit.site.cfg.py
  MAIN_CONFIG
  ${CMAKE_CURRENT_SOURCE_DIR}/lit.cfg.py
)

set(CIRCT_MOCKTURTLE_TEST_DEPENDS
  circt-mockturtle-opt
)

if(CIRCT_MOCKTURTLE_HAS_PLUGIN)
  list(APPEND CIRCT_MOCKTURTLE_TEST_DEPENDS CIRCTMockturtlePlugin circt-opt)
endif()

add_lit_testsuite(check-circt-mockturtle
  "Running the CIRCT mockturtle plugin regression tests"
  ${CMAKE_CURRENT_BINARY_DIR}
  DEPENDS
  ${CIRCT_MOCKTURTLE_TEST_DEPENDS}
)
set_target_properties(check-circt-mockturtle PROPERTIES FOLDER "Tests")
