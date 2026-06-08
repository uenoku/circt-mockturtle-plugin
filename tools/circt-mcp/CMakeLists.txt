set(LLVM_LINK_COMPONENTS
  Support
)

add_llvm_executable(circt-mcp
  main.cpp
)

llvm_update_compile_flags(circt-mcp)

target_link_libraries(circt-mcp
  PRIVATE
  CIRCTQuery
  MLIRSupport
)
