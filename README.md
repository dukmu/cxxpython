# cxxpython

A C++20 Python runtime under development, with multiple OS threads executing
bytecode in one Interpreter and shared heap. Object lifetime uses precise,
nonmoving stop-the-world tracing, not a GIL or Python reference counting.

Only the current runtime is retained. The old interpreter, its standard library,
extension API and tests have been removed. This is an experimental Python subset,
not a complete CPython replacement; full import policy, standard libraries, Python thread/I/O
APIs and the CPython performance acceptance gates remain unfinished.

## Build And Test

Requirements: CMake 3.22+, C11/C++20 compilers, Threads, Boost.Multiprecision/Intrusive headers,
clang-format 14, and host Python for test drivers. Tests use an installed GTest
or CMake's pinned GoogleTest download.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 4
ctest --test-dir build --output-on-failure -j 4
build/cxxpython -c 'print(40 + 2)'
```

For performance builds, enable the standard CMake
`-DCMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE=ON` option in a fresh build directory.
It requires a complete IPO-capable compiler/archive/linker toolchain; configuration
fails with the toolchain diagnostic rather than silently disabling it. See
[Release IPO](doc/development_guide.md#release-ipo) for Clang and test details.

## Run Python

```sh
build/cxxpython                         # terminal REPL
build/cxxpython script.py argument
build/cxxpython -m package argument     # module or package __main__
build/cxxpython -i -c 'value = 40'       # inspect the same namespace
printf 'print(42)\n' | build/cxxpython   # stdin script
build/cxxpython --help
```

The REPL supports multiline input, expression display through `sys.displayhook`,
`_`, exception recovery and `sys.exit`. It uses the same compiler, VM and
`__main__` namespace as scripts. This remains an experimental entrypoint:
readline/history, signal interruption, complete stream/configuration behavior,
installation/isolation and the public `runpy.run_module/run_path` APIs are pending.
`--eval` remains a reserved test diagnostic; it is not a CPython command-line flag.

Set `-DCXXPYTHON_REFERENCE_PYTHON=/path/to/python3.12.14` to use the fixed semantic
reference. The default host interpreter is only a smoke reference.

```sh
cmake --build build --target format
cmake --build build --target format-check
python3 -m venv .cache/benchmark-venv
.cache/benchmark-venv/bin/python -m pip install -r Tools/benchmarks/requirements.txt
.cache/benchmark-venv/bin/python Tools/benchmarks/record.py run \
    --python cxx=build/cxxpython --python cpython=/path/to/python3.12.14 \
    --reference cpython --case nbody --workers 1,2,4 \
    --output .cache/benchmarks/nbody-baseline
```

Performance tables are generated from retained raw runs, never hand-filled.
Run benchmarks after builds and other CPU-heavy work have finished.
See [benchmark methodology and perf commands](Tools/benchmarks/README.md).

## Layout

- `Include/cxxpython/`: current runtime, object, compiler and execution headers.
- `Include/internal/`: compiler-private IR and internal subsystem interfaces.
- `Objects/`: values, types, descriptors, calling and object protocols.
- `Parser/`: independent source decoding, tokenizing and parsing.
- `Python/`: heap/GC/thread state, compilation, verification and VM execution.
- `Programs/`: the single `cxxpython` host entry point.
- `Modules/`: native capabilities and CLI orchestration; Python library policy stays in `Lib/`.
- `Lib/`: reserved for the interpreter's Python standard library.
- `Tests/python/`: host-side Python test drivers and fixed-reference differential tests.
- `Tests/cpp/`: native runtime/GC/concurrency/semantic contract tests.
- `Tools/benchmarks/` and `Tools/scripts/`: measurement and generated-data tools.

See [architecture](doc/architecture_design.md),
[development guide](doc/development_guide.md),
[runtime contracts](doc/foundation_contracts.md) and the authoritative
[Plan 2](cxxpython_foundation_redesign_plan_2.md).
