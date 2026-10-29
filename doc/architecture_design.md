# Current Architecture

The repository follows CPython's directory responsibilities, not its object ABI
or private bytecode format. This document describes only the current runtime.
Historical designs are in `archive/`; Plan 2 remains the full delivery objective.

## Source To Execution

`Programs/cxxpython.cpp` handles host arguments, file reads and diagnostic/output
copying. It enters the runtime through SourceUnit/RunRequest, not a second parser
or VM. There is one executable, `cxxpython`, linked to `cxxpython_core`.

`Parser/parser.cpp` owns SourceBuffer decoding/positions, tokens, the native AST
and parser. `Include/internal/compiler.hpp` is the private contract between that
parser and compiler; it contains no hidden managed Python references.

`Python/symtable.cpp` performs lexical scope analysis. `Python/compile.cpp`
constructs native CFGs and freezes managed constants/Code through CodeBuilder.
`Python/flowgraph.cpp` owns
bytecode construction, stack/control-region verification and disassembly.
Scope analysis is complete before lowering; the emitter does not guess bindings.
AST visitors fail at compile time if a new expression/statement has no explicit
scope or lowering rule. Syntax additions extend these passes rather than adding
another parser, execution path or managed compiler-state layer.
CFG block identity and physical order are separate: lexical region phases remain
contiguous even when branch targets are reserved early. Native region descriptors
and entry/exit markers replay CodeBuilder's existing Loop/Except/Finally/With
operations during freeze. Source control flow uses the same traced cleanup
continuations as native bytecode, not a second source-only exception mechanism.

`Python/ceval.cpp` prepares and schedules invocation-owned Frames, executes the
verified bytecode and calls normal object protocols. Python-to-Python calls do
not grow the C++ stack. `Python/unwind.cpp` handles structured control reasons and
cleanup continuations. `Python/traceback.cpp` owns traceback operations.

## Objects And Lifetime

`Objects/typeobject.cpp` owns type layout/readiness, C3 and immutable type views;
`Objects/typeconstruction.cpp` owns Python class-construction hooks.
`Objects/descrobject.cpp` and `Objects/call.cpp` implement descriptors, attribute
lookup, argument binding and the shared call boundary. `Objects/abstract.cpp`
dispatches typed object protocols. `Objects/longobject.cpp` provides int/bool
arithmetic; `Objects/representation.cpp` provides value representation/hash
operations. `Objects/rangeobject.cpp` owns immutable arbitrary-integer ranges and
their iterators; `Objects/iterobject.cpp` owns iteration and immutable sequence
indexing. `Include/internal/long.hpp` shares the existing integer representation
and publication helper, without a second numeric implementation.
`Objects/object.cpp` contains the initial values, name dictionaries,
core image and common object construction. Generated Unicode tables are private
to Objects and retain their generator/source provenance.

`Python/gc.cpp` owns the precise heap, adjacent allocation metadata, thread-local
spans/root arenas, attachment/parking, epoch rendezvous, transfer roots and core
image lifecycle. These coupled lifetime operations remain in one translation
unit; moving them does not introduce an extra lifetime authority or new locking.

Runtime contracts are in `Include/cxxpython/`. Compiler-private implementation
structures belong in `Include/internal/`. All C++ interfaces remain unstable;
there is currently no extension ABI or CPython ABI compatibility promise.

## Parallel Execution

Several Active attachments may execute bytecode and protocols in one shared
Interpreter. Roots and active Frame state belong to each attached OS thread.
GC uses request/ack/completed epochs and scans only frozen roots. NativeWait
parks a thread without exclusive execution permission on resume.

Mutable shared references use rooted atomic edge operations. Name dictionaries
and type metadata publish immutable snapshots; Code and Function metadata are
immutable. Closure Cell identities are shared, while each call owns its Frame
and fresh local Cells. There is no whole-interpreter ordinary execution lock.

## Tests And Tooling

`Tests/cpp/` tests production runtime interfaces. `Lib/test/` contains host Python
drivers for actual executable differential and measurement-report tests, not a
ported standard library. `Tools/benchmarks/runtime.cpp` runs the current runtime;
`Tools/benchmarks/record.py` retains raw runs and generates comparison tables.

The old implementation and its tests are absent, not disabled behind a build
option. Historical benchmark observations remain evidence of their recorded
commits, not claims that removed features work in the current executable.
