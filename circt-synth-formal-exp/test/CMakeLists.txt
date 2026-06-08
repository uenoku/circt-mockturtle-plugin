configure_lit_site_cfg(
  ${CMAKE_CURRENT_SOURCE_DIR}/lit.site.cfg.py.in
  ${CMAKE_CURRENT_BINARY_DIR}/lit.site.cfg.py
  MAIN_CONFIG
  ${CMAKE_CURRENT_SOURCE_DIR}/lit.cfg.py
)

add_lit_testsuite(check-circt-fraig-lec "Running circt-fraig-lec tests"
  ${CMAKE_CURRENT_BINARY_DIR}
  DEPENDS circt-fraig-lec
)
set_target_properties(check-circt-fraig-lec PROPERTIES FOLDER "Tests")

add_lit_testsuites(CIRCTFRAIGLEC ${CMAKE_CURRENT_BINARY_DIR}
  DEPENDS circt-fraig-lec
)
