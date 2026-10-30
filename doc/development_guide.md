# Development Guide

Follow `cxxpython_foundation_redesign_plan_2.md`, the current runtime contracts
and `AGENTS.md`. The user's repository consolidation retires legacy source now;
it does not waive the remaining language/library/performance acceptance gates.

## Build And Check

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCXXPYTHON_BUILD_BENCHMARKS=ON \
    -DCXXPYTHON_REFERENCE_PYTHON=/path/to/python3.12.14
cmake --build build -j 4
ctest --test-dir build --output-on-failure -j 4
```

The current targets are `cxxpython_core`, `cxxpython`, `test_runtime` and optional
`bench_runtime`. There is no legacy build option or alternate CLI. A system GTest
is used when available; otherwise CMake fetches the pinned source archive.
Set `BOOST_MULTIPRECISION_INCLUDE_DIR` when Boost headers are not on the compiler
search path. Configure sanitizers with the compiler/linker flags in a separate
build directory; never measure performance while compiling or running sanitizers.
Keep reusable inputs and builds under the repository's ignored `.cache/`, for
example `.cache/deps/`, `.cache/cpython31214-build/`, `.cache/build-asan/` and
`.cache/build-tsan/`. `/tmp` is restart-disposable, not durable build state.
Store verification logs under `.cache/` until their results are recorded; retain
release evidence in the documented tracked artifacts. After resuming work, verify
live process handles and actual files before continuing or restarting a build.

## Formatting

Use clang-format 14 and the repository's `.clang-format`: four-space indentation,
function braces on a separate line, control braces on the same line, no tabs and
a 100-column limit. Do not maintain a second hand-formatted C++ style.

```sh
cmake --build build --target format
cmake --build build --target format-check
```

The format targets cover project C++ headers, implementation, tests and benchmark
sources. CTest runs the same dry-run check. Generated Unicode `.inc` data and
third-party source are excluded; change their generator rather than hand-edit
data. Python drivers follow ordinary Python formatting.

## Semantic Changes

- Add focused native contracts and, where supported, executable source tests.
- Define ownership/traversal, synchronization, failure and callback boundaries
  with the behavior. No unrooted object may span an allocating/polling operation.
- Do not add a compatibility interpreter, a hidden GIL or value/workload-specific
  shortcuts. New object types/opcodes must implement their full documented slice.
- Keep the actual capability boundary explicit. The current source subset does
  not imply that removed legacy standard-library or extension features exist.

## Evidence And Commits

Use `Tests/python/test_source.py --binary build/cxxpython --reference ... --report ...`
for retained fixed-reference evidence. Generate benchmark tables with
`Tools/benchmarks/record.py`; retain slow samples and actual build provenance.
Existing `foundation-*` artifact names identify the historical series and are
retained, even though the executable/target names are now consolidated.

Append or prepend verified progress in `doc/development_status.md`; never rewrite
earlier entries. Keep stable commits timely and exclude unrelated user changes.
