# CIRCT Experiment

This is an out-of-tree CIRCT project for experimental passes and tools.
It builds:

* `circt-mockturtle-opt`, a standalone optimizer driver.
* `CIRCTMockturtlePlugin.so`, a pass plugin loadable by `circt-opt`.
* `CIRCTMockturtle`, a small pass library that depends on mockturtle.
* `circt-fraig-lec`, a formal equivalence and bounded-model-checking tool
  imported from `circt-synth-formal/`.

Tools live under `tools/` and plugins live under `plugins/`.
Formal verification code lives under `circt-synth-formal/`.

This repository pins the CIRCT version as the `circt` git submodule. Clone with
submodules, or initialize them after cloning:

```sh
git submodule update --init --recursive
```

Build the pinned CIRCT checkout first:

```sh
cmake -G Ninja -S circt/llvm/llvm -B circt-build \
  -DLLVM_ENABLE_PROJECTS=mlir \
  -DLLVM_EXTERNAL_PROJECTS=circt \
  -DLLVM_EXTERNAL_CIRCT_SOURCE_DIR=$PWD/circt \
  -DLLVM_TARGETS_TO_BUILD=host \
  -DLLVM_INSTALL_UTILS=ON

ninja -C circt-build circt-opt FileCheck count
```

Then configure this project against that CIRCT build tree. CMake fetches
mockturtle from the upstream `lsils/mockturtle` repository by default:

```sh
cmake -G Ninja -S . -B build \
  -DCIRCT_DIR=$PWD/circt-build/lib/cmake/circt \
  -DMLIR_DIR=$PWD/circt-build/lib/cmake/mlir \
  -DLLVM_DIR=$PWD/circt-build/lib/cmake/llvm
```

To use an existing local mockturtle checkout instead of fetching:

```sh
cmake -G Ninja -S . -B build \
  -DCIRCT_DIR=$PWD/circt-build/lib/cmake/circt \
  -DMLIR_DIR=$PWD/circt-build/lib/cmake/mlir \
  -DLLVM_DIR=$PWD/circt-build/lib/cmake/llvm \
  -DFETCHCONTENT_SOURCE_DIR_MOCKTURTLE=/path/to/mockturtle
```

Set `MOCKTURTLE_GIT_TAG` to build against a different upstream revision.

Build and test:

```sh
ninja -C build check-circt-experiment
```

Use the driver directly:

```sh
build/bin/circt-mockturtle-opt input.mlir --synth-mockturtle-aig-stats
```

Or load the plugin into `circt-opt`:

```sh
circt-opt input.mlir \
  --load-pass-plugin=build/lib/CIRCTMockturtlePlugin.so \
  --pass-pipeline='builtin.module(hw.module(synth-mockturtle-aig-stats))'
```
