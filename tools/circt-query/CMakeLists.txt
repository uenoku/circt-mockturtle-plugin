set(LLVM_LINK_COMPONENTS
  Support
)

add_llvm_executable(circt-query
  main.cpp
)

llvm_update_compile_flags(circt-query)

target_link_libraries(circt-query
  PRIVATE
  CIRCTQuery
  MLIRSupport
)
