# Development Guide

Follow `cxxpython_foundation_redesign_plan_2.md`, the current runtime contracts
and `AGENTS.md`. The user's repository consolidation retires legacy source now;
it does not waive the remaining language/library/performance acceptance gates.

## Build And Check

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
    -DCXXPYTHON_REFERENCE_PYTHON=/path/to/python3.12.14
cmake --build build -j 4
ctest --test-dir build --output-on-failure -j 4
```

The current targets include `cxxpython_parser`, `cxxpython_core`, `cxxpython`,
`test_parser`, `test_runtime` and the native `parser_probe` development tool.
Benchmarks use host-side Python scripts, not a compiled performance target.
There is no legacy build option or alternate CLI. A system GTest
is used when available; otherwise CMake fetches the pinned source archive.
OpenSSL 3 development headers and libcrypto are required for native hashlib
capabilities. Portable BLAKE2 kernels are vendored from the pinned CPython tree;
their CC0 license is retained in `doc/licenses/blake2-CC0-1.0.txt`.
Set `BOOST_MULTIPRECISION_INCLUDE_DIR` when Boost headers are not on the compiler
search path. `CXXPYTHON_STDLIB_DIR` defaults to the repository's `Lib/`; the source
bootstrap reads this directory, which must remain available to the executable.
`CXXPYTHON_PREFIX` defaults to the repository root; sys prefix/base-prefix fields
report this configured resource root, and sys._stdlib_dir reports the configured
stdlib directory. Search-path mutations do not redefine these fields. These
development settings are not completed installation, virtual-environment or
isolated path configuration.
Use Clang 21.1.8 for strict acceptance, with both C and C++ compilers selected in
a separate build directory. Project targets enable -Wall -Wextra -Wpedantic
-Werror; do not suppress warnings globally or accept a GCC-only pass in its place.
Configure sanitizers with the compiler/linker flags in a separate
build directory; never measure performance while compiling or running sanitizers.
Use `-DCXXPYTHON_TEST_TIMEOUT=120` for instrumented debug suites; normal builds
default to 15 seconds per native test. Preserve elapsed times and investigate
actual failures rather than removing slow correctness cases.
The unittest CLI driver uses that same product-process budget and a fixed
30-second reference budget; its outer watchdog accounts for all four sequential
scenarios. Direct driver runs default to 30 seconds per product process.
Keep reusable inputs and builds under the repository's ignored `.cache/`, for
example `.cache/deps/`, `.cache/cpython31214-build/`, `.cache/build-asan/` and
`.cache/build-tsan/`. `/tmp` is restart-disposable, not durable build state.
Store verification logs under `.cache/` until their results are recorded; retain
release evidence in the documented tracked artifacts. After resuming work, verify
live process handles and actual files before continuing or restarting a build.
The pinned reference must expose its genuine compiled extension modules through
its generated build/lib.* search directory. A module merely present in Modules/
is not automatically importable; finish that build setup rather than inserting
product Lib into the reference path or changing reference/library semantics.

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
sources. CTest runs the same dry-run check. Generated parser/grammar headers, Unicode tables and
third-party source are excluded; change their generator rather than hand-edit
data. Python drivers follow ordinary Python formatting.

## Semantic Changes

- Python-visible behavior belongs in `Tests/cases/<name>/test.py` plus reference-
  generated `stdout.bin`; optional `stdin.bin` is input, not the test script.
  Do not put runnable Python in C++ string literals calling SourceUnit. Run
  `python3 Tests/python/test_cases.py --binary build/cxxpython` without rebuilding
  tests. CTest automatically registers `Scripts.Cases.<name>` and
  `Scripts.Reference.<name>` for every fixture directory, running the same byte-exact fixtures
  through the product and pinned reference. `test_cases.py --timeout SECONDS`
  sets a per-script process deadline; CMake uses at least 30 seconds, or the
  configured native-test budget for instrumented builds. See `Tests/cases/README.md` for stderr
  and exit status. Keep C++ for native-only ownership/IR/quota/fault/thread tests.
- Host terminal/process-signal/CLI boundaries belong in `Tests/python/`; compare
  actual child output and exit status against the fixed reference where applicable.
  `test_unittest_cli.py` covers real -m unittest execution without requiring the
  product to implement subprocess just to launch its own test process.
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
`Tools/benchmarks/record.py run`; retain slow samples and actual build provenance
under ignored `.cache/benchmarks/`. The former C++ foundation and one-off
regex/lexing drivers and generated doc snapshots have been retired. See
`Tools/benchmarks/README.md` for fixed-work threading, calibration, warmups,
standard pyperf export, comparisons and separate profiling. Correctness tests
of the harness are `Benchmarks.Contracts`; they do not impose timing thresholds.

Append or prepend verified progress in `doc/development_status.md`; never rewrite
earlier entries. Keep stable commits timely and exclude unrelated user changes.
