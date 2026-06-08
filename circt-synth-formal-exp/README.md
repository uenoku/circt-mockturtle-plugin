# CIRCT FRAIG LEC Example

This is a standalone CIRCT project that proves equivalence by lowering
`verif.lec` to a hardware miter and running CIRCT's synth functional-reduction
flow. It can also prove simple bounded safety checks by unrolling `verif.bmc`
into the same kind of FRAIG-friendly miter.

The flow is:

1. Lower each `verif.lec` operation to an `hw.module` with the LEC block
   arguments as inputs, predeclared state cut-point inputs, and one
   `fail : i1` output.
2. Match currently exposed named registers and treat their current-state values
   as cut-points connected through `synth.choice`.
3. Run CSE/canonicalization/CSE.
4. Repeatedly inline one exposed `hw.instance` layer, match newly exposed
   registers, and run CSE/canonicalization/CSE.
5. Prune non-miter helper modules once the miter has been materialized.
6. Lower `comb` operations to AIG-style `synth` operations.
7. Bit-blast word-level logic.
8. Run `synth-functional-reduction`.
9. Resolve proven `synth.choice` nodes and canonicalize the miter.
10. Report equivalence when every miter `fail` output is constant zero, with a
    per-output and per-register-next-state proven/not-proven breakdown.

For `verif.bmc`, the lower step creates one `bmc_miter` per BMC op. The `init`
region is cloned once, the `circuit` and `loop` regions are cloned for each
bounded step, fresh non-clock circuit inputs become miter inputs, register
state is threaded through the unrolled frames, and each `verif.assert` becomes
a labeled check bit guarded by the accumulated `verif.assume` conditions for
the path prefix. A proven BMC miter is reported as `safe within bound`; this is
not an unbounded safety proof.

Configure it against a CIRCT build or install tree:

```sh
cd examples/circt-fraig-lec
cmake -G Ninja -S . -B build \
  -DCIRCT_DIR=<path-to-circt-build-or-install>/lib/cmake/circt \
  -DMLIR_DIR=<path-to-mlir-build-or-install>/lib/cmake/mlir
ninja -C build circt-fraig-lec
```

Run it on a `verif.lec` input:

```sh
build/bin/circt-fraig-lec test/commuted-add.mlir
```

Run it on a `verif.bmc` input:

```sh
build/bin/circt-fraig-lec test/bmc-assume-guards-assert.mlir
```

Run it on a BTOR2 input:

```sh
build/bin/circt-fraig-lec --btor2-bound=2 test/btor2-state-fails.btor2
```

BTOR2 files are recognized by `.btor` and `.btor2` extension, or explicitly
with `--input-format=btor2`. Use `--emit-imported` to inspect the `verif.bmc`
materialized from BTOR2 before miter lowering. Imported BTOR2 state updates are
modeled as one transition per bounded step, matching BTOR2 `next` semantics,
rather than as a two-phase clock toggle.

Use `--btor2-sweep --btor2-bound=N` to run bounded checking for every bound
from 1 through `N`, stopping at the first counterexample. If no counterexample
is found, the result is only safe within that maximum bound; this is not an
unbounded proof.

SAT calls are unbounded by default. Use `--conflict-limit=N` only when an
explicit per-query SAT budget is desired; a budgeted run may leave checks
unknown. The same budget is applied to miter counterexample generation,
functional reduction, the BTOR2 BMC precheck, and the prototype BTOR2 PDR
engine.

The normal report includes the aggregate miter result followed by each tracked
check bit:

```text
lec_miter: not proven
  output 0: proven
  register acc next-state: not proven
```

A BMC report uses the same per-check format:

```text
bmc_miter: not proven
  step 0 assertion state_zero: proven
  step 1 assertion state_zero: not proven
```

Use `--bmc-k-induction` to also build an experimental induction-step miter. The
base `bmc_miter` still checks frames `0..k-1` from the declared initial state.
The additional `bmc_induction_miter` starts registers from symbolic
`state_regN` inputs, assumes earlier unrolled assertions hold, and checks the
last frame. A proven induction-step miter is reported as `inductive step
proven`. A failing induction miter does not disprove the property; it means the
asserted property was not inductive under the current assumptions.

Use `--bmc-recurrence` to also build a recurrence-diameter miter. This checks
whether every path of the selected length must repeat a state. If the bounded
BMC miter is `safe within bound` and the recurrence miter is `complete within
bound`, the bounded result is complete for that finite transition system. This
is sound but can be expensive and is mainly a baseline completeness engine.

Use `--btor2-pdr-depth=N` to run the prototype PDR engine after importing BTOR2
as `verif.bmc`. This is an experimental IC3/PDR-style unbounded prover for a
small bit-blasted transition-system subset; it is intended as the proof-engine
seed, not yet as the benchmark-complete path.

When the PDR path finds a counterexample through its bounded precheck, the trace
prints state values for each frame and any primary input assignments used to
drive the transition between frames.

PDR div/mod normalization is bounded by
`--btor2-pdr-divmod-unknown-bits=N`, which defaults to 16. Raising this can
cover more word-level benchmarks, but may significantly increase the size of
the normalized transition logic. `--btor2-pdr-blocked-cube-limit=N` bounds PDR
search work by returning bounded unknown after blocking `N` cubes; this is
useful for benchmark sweeps where a shell timeout would otherwise discard the
partial solver result. `--conflict-limit=N` can additionally bound expensive
individual SAT queries inside the BMC precheck or PDR search.

For repeatable smoke testing on the SOSY-Lab word-level HWMc benchmarks, use the
helper script:

```sh
scripts/bench-word-level-hwmc.sh \
  --root ../../word-level-hwmc-benchmarks \
  --category bv \
  --limit 20 \
  --depth 4 \
  --precheck-depth 0 \
  --divmod-bits 16 \
  --pdr-blocked-cube-limit 0 \
  --conflict-limit -1 \
  --timeout 15 \
  --order name \
  --log-dir bench-logs \
  --summary-table \
  --detail-table
```

The script runs `circt-fraig-lec --btor2-pdr-depth=N` over sorted `.btor2`
files and reports per-file results plus a summary bucketed as proven,
counterexample, unknown, timeout, or unsupported. Use `--order size` to run
smaller files first when smoking a mixed benchmark checkout where lexical order
starts with large instances. Use `--summary-table` to also print a markdown row
with `success`, `timeout`, and `fail` totals; `success` means the tool completed
with proven, counterexample, or bounded unknown, while `fail` covers unsupported
cases and hard errors. Use `--detail-table` to print the same classification
for each benchmark file, together with the exact status and final diagnostic
line. Use `--log-dir DIR` to keep the complete stdout/stderr for each benchmark
file. Use `--conflict-limit N` and `--pdr-blocked-cube-limit N` to turn some
long-running SAT/PDR searches into bounded unknown results. Extra tool options
can be forwarded with repeated `--tool-arg ARG`, for example to collect
`--mlir-print-ir-after-all` output in those logs when debugging a pass failure.

Current smoke baseline, using
`--depth 4 --timeout 5 --limit 30 --pdr-blocked-cube-limit 10 --conflict-limit 100`:

| set | order | total | success | timeout | fail | proven | counterexample | unknown | unsupported | error |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| `word-level-hwmc-benchmarks/bv/btor2/fuzzbtor2` | name | 30 | 30 | 0 | 0 | 17 | 13 | 0 | 0 | 0 |
| `word-level-hwmc-benchmarks/array/btor2/fuzzbtor2` | name | 30 | 27 | 3 | 0 | 9 | 18 | 0 | 0 | 0 |
| `btor-benchmarks` | size | 30 | 29 | 1 | 0 | 0 | 0 | 29 | 0 | 0 |

Use `--emit-miter` to stop after miter construction, `--emit-reduced` to print
the reduced IR before checking, or `--stop-after={lower,match,inline,prune}` to
inspect an individual miter-construction stage. For PDR debugging,
`--emit-pdr-transition` prints the lowered transition module, while
`--emit-pdr-normalized-transition` prints the same transition after aggregate,
comb, and synth normalization.

The instance flattening and sequential cut-point support are intentionally local
to the LEC-to-miter IR rewrite. The tool currently flattens unparameterized
`hw.module` instances, including nested instances. It supports named
`seq.compreg`, `seq.compreg.ce`, and synchronous-reset `seq.firreg` operations
directly in the two `verif.lec` regions or below flattened instances. Registers
inside instances are matched by hierarchical names such as `u/acc`. Both sides
must contain the same register names with the same types. Unsupported
sequential operations, unnamed registers, duplicate register names, async-reset
`seq.firreg` operations, extern modules, and parameterized instances are
rejected.

The BMC lowering currently supports integer non-clock inputs, at most one
clock, integer or unit initial register values, `seq.to_clock`/`seq.from_clock`
in the BMC regions, and unclocked single-bit `verif.assert`/`verif.assume`.
Assertions or assumptions nested inside BMC instances must be inlined before
this lowering; registers below BMC instances must be externalized into
`verif.bmc` state arguments first.

The BTOR2 importer currently supports the bit-vector subset emitted by CIRCT's
HW-to-BTOR2 flow: `sort bitvec`, `input`, `state`, constant forms, `init`,
`next`, `bad`, `constraint`, `slice`, `ite`, common arithmetic/bitwise ops,
comparisons, reductions, and sign/zero extension. BTOR2 `bad` lines become
`verif.assert` operations over the negated bad expression, and `constraint`
lines become `verif.assume` operations.
