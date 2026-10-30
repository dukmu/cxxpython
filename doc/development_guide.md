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

The current targets include `cxxpython_parser`, `cxxpython_core`, `cxxpython`,
`test_parser`, `test_runtime`, the native `parser_probe` development tool and
optional `bench_runtime`. There is no legacy build option or alternate CLI. A system GTest
is used when available; otherwise CMake fetches the pinned source archive.
Set `BOOST_MULTIPRECISION_INCLUDE_DIR` when Boost headers are not on the compiler
search path. `CXXPYTHON_STDLIB_DIR` defaults to the repository's `Lib/`; the source
bootstrap reads this directory, which must remain available to the executable.
This development setting is not a completed installed/isolated path configuration.
Configure sanitizers with the compiler/linker flags in a separate
build directory; never measure performance while compiling or running sanitizers.
Use `-DCXXPYTHON_TEST_TIMEOUT=120` for instrumented debug suites; normal builds
default to 15 seconds per native test. Preserve elapsed times and investigate
actual failures rather than removing slow correctness cases.
Keep reusable inputs and builds under the repository's ignored `.cache/`, for
example `.cache/deps/`, `.cache/cpython31214-build/`, `.cache/build-asan/` and
`.cache/build-tsan/`. `/tmp` is restart-disposable, not durable build state.
Store verification logs under `.cache/` until their results are recorded; retain
release evidence in the documented tracked artifacts. After resuming work, verify
live process handles and actual files before continuing or restarting a build.

The parser can be built independently with `cmake -S Parser -B .cache/build-parser`
and `cmake --build .cache/build-parser`, without Python, Boost or GTest. Its fixed
grammar and generation provenance are described in Tools/peg_generator/README.md.
Use `regen-parser` after grammar/backend changes and `parser-generated-check` to
verify the checked-in output. Do not restore the removed handwritten parser.

## Formatting

Use clang-format 14 and the repository's `.clang-format`: four-space indentation,
function braces on a separate line, control braces on the same line, no tabs and
a 100-column limit. Do not maintain a second hand-formatted C++ style.

```sh
cmake --build build --target format
cmake --build build --target format-check
```

The format targets cover project C++ headers, implementation, tests and benchmark
sources. CTest runs the same dry-run check. Generated parser/grammar headers, Unicode `.inc` data and
third-party source are excluded; change their generator rather than hand-edit
data. Python drivers follow ordinary Python formatting.

## Semantic Changes

- Python-visible behavior belongs in `Tests/cases/<name>/stdin.bin` plus reference-
  generated `stdout.bin`, not C++ string literals calling SourceUnit. Run
  `python3 Tests/python/test_cases.py --binary build/cxxpython` without rebuilding
  tests. `Scripts.Cases` and `Scripts.Reference` run the same byte-exact fixtures
  through the product and pinned reference. See `Tests/cases/README.md` for stderr
  and exit status. Keep C++ for native-only ownership/IR/quota/fault/thread tests.
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
