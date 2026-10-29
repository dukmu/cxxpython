# cxxpython

A C++20 Python runtime under development, with multiple OS threads executing
bytecode in one Interpreter and shared heap. Object lifetime uses precise,
nonmoving stop-the-world tracing, not a GIL or Python reference counting.

Only the current runtime is retained. The old interpreter, its standard library,
extension API and tests have been removed. This is an experimental Python subset,
not a complete CPython replacement; imports, standard libraries, Python thread/I/O
APIs and the CPython performance acceptance gates remain unfinished.

## Build And Test

Requirements: CMake 3.22+, a C++20 compiler, Threads, Boost.Multiprecision headers,
clang-format 14, and host Python for test drivers. Tests use an installed GTest
or CMake's pinned GoogleTest download.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCXXPYTHON_BUILD_BENCHMARKS=ON
cmake --build build -j 4
ctest --test-dir build --output-on-failure -j 4
build/cxxpython -c 'def f(x): return x + 1' --eval 'f(41)'
```

Set `-DCXXPYTHON_REFERENCE_PYTHON=/path/to/python3.12.14` to use the fixed semantic
reference. The default host interpreter is only a smoke reference.

```sh
cmake --build build --target format
cmake --build build --target format-check
python3 Tools/benchmarks/record.py --binary build/bench_runtime \
    --reference /path/to/python3.12.14 --series my-run --runs 3
```

Performance tables are generated from retained raw runs, never hand-filled.
Run benchmarks after builds and other CPU-heavy work have finished.

## Layout

- `Include/cxxpython/`: current runtime, object, compiler and execution headers.
- `Include/internal/`: compiler-private AST/scope/CFG structures.
- `Objects/`: values, types, descriptors, calling and object protocols.
- `Parser/`: independent source decoding, tokenizing and parsing.
- `Python/`: heap/GC/thread state, compilation, verification and VM execution.
- `Programs/`: the single `cxxpython` host entry point.
- `Lib/`: reserved for the interpreter's Python standard library.
- `Tests/python/`: host-side Python test drivers and fixed-reference differential tests.
- `Tests/cpp/`: native runtime/GC/concurrency/semantic contract tests.
- `Tools/benchmarks/` and `Tools/scripts/`: measurement and generated-data tools.

See [architecture](doc/architecture_design.md),
[development guide](doc/development_guide.md),
[runtime contracts](doc/foundation_contracts.md) and the authoritative
[Plan 2](cxxpython_foundation_redesign_plan_2.md).
