# Script Cases

Each directory contains `stdin.bin` (Python source) and `stdout.bin` (expected
stdout bytes). The driver feeds source to the real interpreter's stdin and
compares stdout, stderr and exit status exactly. `stderr.bin` defaults to empty;
`exitcode.txt` defaults to zero. No C++ test compilation is required when editing
or adding a script case.
Each process runs in a fresh scratch directory; filesystem tests cannot leave
state in the checked-in fixture or poison the next run.

```sh
python3 Tests/python/test_cases.py --binary build/cxxpython
python3 Tests/python/test_cases.py --binary build/cxxpython --case CASE_NAME
```

Golden outputs come from the pinned CPython 3.12.14 reference, not the runtime
under test. For example, from a case directory:

```sh
/path/to/python3.12.14 < stdin.bin > stdout.bin
```

Avoid addresses, timings and platform-dependent output. Assert their actual
semantic property in Python rather than normalizing away differences in the
driver. Cyclic GC cases use actual cycles, so immediate reference-counted
deallocation is not confused with a tracing collection. C++ tests are reserved
for contracts inaccessible to a Python script: exact roots, allocation quotas,
host faults and shared-heap native thread control.

`stdlib_cpython_bisect` retains CPython's original precomputed test dataset and
checks its bounded searches. `Tests/python/test_stdlib.py` also executes upstream
heapq doctest examples through the product's normal single-mode compiler. These
are real upstream tests/data, not a substitute declaration that the complete
unittest/test dependency closure already works.
