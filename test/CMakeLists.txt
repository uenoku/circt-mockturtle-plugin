configure_lit_site_cfg(
  ${CMAKE_CURRENT_SOURCE_DIR}/lit.site.cfg.py.in
  ${CMAKE_CURRENT_BINARY_DIR}/lit.site.cfg.py
  MAIN_CONFIG
  ${CMAKE_CURRENT_SOURCE_DIR}/lit.cfg.py
)

set(CIRCT_EXPERIMENT_TEST_DEPENDS
  circt-experiment-opt
  circt-experiment-translate
  circt-fraig-lec
)

if(CIRCT_EXPERIMENT_HAS_PLUGIN)
  list(APPEND CIRCT_EXPERIMENT_TEST_DEPENDS CIRCTExperimentPlugin circt-opt)
endif()

add_lit_testsuite(check-circt-experiment
  "Running the CIRCT experiment regression tests"
  ${CMAKE_CURRENT_BINARY_DIR}
  DEPENDS
  ${CIRCT_EXPERIMENT_TEST_DEPENDS}
)
set_target_properties(check-circt-experiment PROPERTIES FOLDER "Tests")
