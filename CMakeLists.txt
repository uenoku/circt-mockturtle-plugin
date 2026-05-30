cmake_minimum_required(VERSION 3.20.0)
project(circt-mockturtle-plugin LANGUAGES CXX C)

set(CMAKE_BUILD_WITH_INSTALL_NAME_DIR ON)

set(CMAKE_CXX_STANDARD 17 CACHE STRING "C++ standard to conform to")
set(CMAKE_CXX_STANDARD_REQUIRED YES)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  find_package(CIRCT REQUIRED CONFIG)

  message(STATUS "Using CIRCTConfig.cmake in: ${CIRCT_DIR}")
  message(STATUS "Using MLIRConfig.cmake in: ${MLIR_DIR}")
  message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

  set(LLVM_RUNTIME_OUTPUT_INTDIR ${CMAKE_BINARY_DIR}/bin)
  set(LLVM_LIBRARY_OUTPUT_INTDIR ${CMAKE_BINARY_DIR}/lib)
  set(MLIR_BINARY_DIR ${CMAKE_BINARY_DIR})

  list(APPEND CMAKE_MODULE_PATH "${CIRCT_CMAKE_DIR}")
  list(APPEND CMAKE_MODULE_PATH "${MLIR_CMAKE_DIR}")
  list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")

  include(TableGen)
  include(AddLLVM)
  include(AddMLIR)
  include(AddCIRCT)
  include(HandleLLVMOptions)
endif()

set(CIRCT_MOCKTURTLE_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(CIRCT_MOCKTURTLE_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR})

set(MOCKTURTLE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../mockturtle"
    CACHE PATH "Path to a mockturtle source checkout")

if(NOT EXISTS "${MOCKTURTLE_SOURCE_DIR}/include/mockturtle/mockturtle.hpp")
  message(FATAL_ERROR
    "MOCKTURTLE_SOURCE_DIR must point to a mockturtle source checkout")
endif()

add_library(mockturtle_headers INTERFACE)
add_definitions(
  -DFMT_HEADER_ONLY
  -DDISABLE_NAUTY
  -DLIN64
  -DABC_NAMESPACE=pabc
  -DABC_NO_USE_READLINE
)
target_compile_definitions(mockturtle_headers INTERFACE
  FMT_HEADER_ONLY
  DISABLE_NAUTY
  LIN64
  ABC_NAMESPACE=pabc
  ABC_NO_USE_READLINE
)
target_include_directories(mockturtle_headers SYSTEM INTERFACE
  "${MOCKTURTLE_SOURCE_DIR}/include"
  "${MOCKTURTLE_SOURCE_DIR}/lib/abcesop"
  "${MOCKTURTLE_SOURCE_DIR}/lib/abcsat"
  "${MOCKTURTLE_SOURCE_DIR}/lib/bill"
  "${MOCKTURTLE_SOURCE_DIR}/lib/fmt"
  "${MOCKTURTLE_SOURCE_DIR}/lib/json"
  "${MOCKTURTLE_SOURCE_DIR}/lib/kitty"
  "${MOCKTURTLE_SOURCE_DIR}/lib/lorina"
  "${MOCKTURTLE_SOURCE_DIR}/lib/parallel_hashmap"
  "${MOCKTURTLE_SOURCE_DIR}/lib/percy"
  "${MOCKTURTLE_SOURCE_DIR}/lib/rang"
)

set(STATIC_LIBABC ON CACHE BOOL "Build mockturtle ABC support as static libs"
    FORCE)
if(NOT TARGET libabcesop)
  add_subdirectory("${MOCKTURTLE_SOURCE_DIR}/lib/abcesop"
                   "${CMAKE_BINARY_DIR}/mockturtle-abcesop"
                   EXCLUDE_FROM_ALL)
endif()
if(NOT TARGET libabcsat)
  add_subdirectory("${MOCKTURTLE_SOURCE_DIR}/lib/abcsat"
                   "${CMAKE_BINARY_DIR}/mockturtle-abcsat"
                   EXCLUDE_FROM_ALL)
endif()
foreach(dep libabcesop libabcsat)
  if(TARGET ${dep})
    set_target_properties(${dep} PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_compile_options(${dep} PRIVATE
      $<$<COMPILE_LANGUAGE:CXX>:-fexceptions -frtti -w>
    )
  endif()
endforeach()

include_directories(${LLVM_INCLUDE_DIRS})
include_directories(${MLIR_INCLUDE_DIRS})
include_directories(${CIRCT_INCLUDE_DIRS})
include_directories(${CIRCT_MOCKTURTLE_SOURCE_DIR}/include)
include_directories(${CIRCT_MOCKTURTLE_BINARY_DIR}/include)
link_directories(${LLVM_BUILD_LIBRARY_DIR})
link_directories(${CIRCT_LIBRARY_DIRS})
add_definitions(${LLVM_DEFINITIONS})

option(CIRCT_MOCKTURTLE_INCLUDE_TESTS
  "Generate build targets for the CIRCT mockturtle plugin tests." ON)

add_subdirectory(include)
add_subdirectory(lib)
add_subdirectory(circt-mockturtle-opt)

set(CIRCT_MOCKTURTLE_HAS_PLUGIN 0)
if(NOT WIN32 AND TARGET circt-opt)
  add_subdirectory(circt-mockturtle-plugin)
  set(CIRCT_MOCKTURTLE_HAS_PLUGIN 1)
endif()

if(CIRCT_MOCKTURTLE_INCLUDE_TESTS)
  add_subdirectory(test)
endif()
