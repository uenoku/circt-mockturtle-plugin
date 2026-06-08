// RUN: circt-experiment-translate --mockturtle-mlir-to-cpp %s | FileCheck %s
// RUN: rm -rf %t && circt-experiment-translate --mockturtle-mlir-to-cpp --mockturtle-repro-dir=%t %s | FileCheck %s --check-prefix=REPRO-MSG
// RUN: FileCheck %s --check-prefix=CPP --input-file=%t/repro.cpp
// RUN: FileCheck %s --check-prefix=CMAKE --input-file=%t/CMakeLists.txt
// RUN: FileCheck %s --check-prefix=README --input-file=%t/README.md

// CHECK: #include <mockturtle/networks/aig.hpp>
// CHECK: #include <mockturtle/networks/xag.hpp>
// CHECK: void run_mockturtle_refactor(
// CHECK: mockturtle::refactoring_params const &ps = {})
// CHECK: void run_mockturtle_xag_balancing(
// CHECK: mockturtle::xag_balancing_params const &ps = {})
// CHECK: void run_mockturtle_xmg_resubstitution(
// CHECK: mockturtle::resubstitution_params const &ps = {})

// CHECK-LABEL: mockturtle::xag_network build_xag()
// CHECK: auto a = ntk.create_pi(); // a
// CHECK: auto b = ntk.create_pi(); // b
// CHECK: auto c = ntk.create_pi(); // c
// CHECK: auto n = ntk.create_and(a, b);
// CHECK: auto n_1 = ntk.create_xor(n, !c);
// CHECK: ntk.create_po(n_1); // out

// CHECK-LABEL: mockturtle::xmg_network build_xmg()
// CHECK: ntk.create_maj
// CHECK: ntk.create_xor

// REPRO-MSG: wrote mockturtle repro to

// CPP: #include <mockturtle/networks/xag.hpp>
// CPP-LABEL: mockturtle::xag_network build_xag()
// CPP: ntk.create_po

// CMAKE: set(MOCKTURTLE_GIT_REPOSITORY "https://github.com/lsils/mockturtle.git"
// CMAKE: set(MOCKTURTLE_GIT_TAG "9f3a6c94327ee26a7cdcd998a38f5bb2131b956a"
// CMAKE: FetchContent_Declare(
// CMAKE: mockturtle
// CMAKE: GIT_REPOSITORY "${MOCKTURTLE_GIT_REPOSITORY}"
// CMAKE: GIT_TAG "${MOCKTURTLE_GIT_TAG}"
// CMAKE: add_executable(repro repro.cpp)
// CMAKE: target_link_libraries(repro PRIVATE libabcesop libabcsat)

// README: cmake -S . -B build
// README: FETCHCONTENT_SOURCE_DIR_MOCKTURTLE

hw.module @xag(in %a : i1, in %b : i1, in %c : i1, out out : i1) {
  %0 = synth.aig.and_inv %a, %b : i1
  %1 = synth.xor_inv %0, not %c : i1
  hw.output %1 : i1
}

hw.module @xmg(in %a : i1, in %b : i1, in %c : i1, out out : i1) {
  %0 = synth.majority %a, not %b, %c : i1
  %1 = synth.xor_inv %0, %a : i1
  hw.output %1 : i1
}
