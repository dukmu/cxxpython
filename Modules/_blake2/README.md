# Portable BLAKE2 Kernels

`impl/blake2.h`, `blake2-impl.h`, `blake2b-ref.c` and `blake2s-ref.c` are copied
unchanged from CPython 3.12.14 `Modules/_blake2/impl/`, retained locally under
`.cache/deps/Python-3.12.14`. Original author and CC0 notices remain in each file;
the full dedication is in `doc/licenses/blake2-CC0-1.0.txt`.

`Modules/_hashopenssl.cpp` adapts their native state to this runtime's boxed hash
types, tracing lifetime and per-state synchronization. It does not use CPython's
object layout, reference counting or GIL. Do not format the vendored kernels.
