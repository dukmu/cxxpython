# Current Architecture

The repository follows CPython's directory responsibilities, not its object ABI
or private bytecode format. This document describes only the current runtime.
Historical designs are in `archive/`; Plan 2 remains the full delivery objective.

## Source To Execution

`Programs/cxxpython.cpp` handles host arguments, file reads and diagnostic/output
copying. It enters the runtime through SourceUnit/RunRequest, not a second parser
or VM. There is one executable, `cxxpython`, linked to `cxxpython_core`.

`Parser/tokenizer.cpp` and the automatically generated
`Parser/generated_parser.cpp` form a standalone UTF-8 Python 3.12 syntax library.
`Grammar/python.gram` and `Grammar/Tokens` are fixed CPython 3.12.14 inputs;
`Tools/peg_generator/` retains upstream pegen's grammar reader/left-recursion
analysis and this project's C++ output backend. Generated rules construct a native
immutable CST using per-parse memo state, not CPython AST objects. The independent
Parser CMake entry needs only a C++ compiler and standard library. Host Python is
needed for explicit regeneration/checks, not normal parser builds or execution.

`Parser/source.cpp` owns the runtime's SourceBuffer decoding and diagnostics.
`Parser/ast.cpp` adapts the generated CST to the existing native compiler AST;
it does not recognize grammar or retain a handwritten parser fallback.
`Include/internal/compiler.hpp` is the private compiler IR contract and contains
no hidden managed Python references. Full grammar recognition and executable
language support are separate: missing AST/compiler/VM capabilities remain
explicit errors after parsing, not rules deleted from the upstream grammar.

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
`Python/bltinmodule.cpp` owns the ordinary __build_class__ builtin, coordinating
base expansion, metaclass preparation, a normal class-body Frame and existing
type construction. It is not a separate class VM or lifetime authority.
`Python/import.cpp` owns the experimental registered/file-source/static-native
initialization transaction and bootstrap coordination. Host-only per-Interpreter
records coordinate owners/waiters; the managed module table is the only completed
module authority. File lookup snapshots Python-visible search paths, then reads
and compiles while parked. Source import statements lower through the existing
scope/CFG passes into verified import opcodes and normal calls/unwinding.
`Python/extension.cpp` bridges the versioned C API and POSIX dynamic libraries into
that same transaction. `Modules/_impmodule.cpp` supplies source/code execution and
loader callbacks; `Lib/importlib/` defines ModuleSpec, loader classes and Python
import/reload policy. Cold bootstrap alone coordinates one owner; after boot,
unrelated module bodies retain independent leases and execute concurrently.
Seed modules and Python bootstrap use the same publication/rollback transaction;
no native cache duplicates Python classes or module namespaces. Reload preserves
the existing module/dictionary and does not remove it on body failure. Full
finders/path hooks, namespace path refresh and bytecode caches remain gates.

## Objects And Lifetime

`Objects/typeobject.cpp` owns type layout/readiness, C3 and immutable type views;
`Objects/typeconstruction.cpp` owns Python class-construction hooks.
`Objects/descrobject.cpp` and `Objects/call.cpp` implement descriptors, attribute
lookup, argument binding and the shared call boundary. `Objects/abstract.cpp`
dispatches typed object protocols. `Objects/longobject.cpp` provides int/bool
construction and arithmetic; `Objects/floatobject.cpp` owns the immutable native
float payload, conversions, representation, arithmetic, comparison and hash. Numeric text
conversion shares Unicode decimal/whitespace helpers in `Include/internal/long.hpp`.
Float literals share the native conversion helper; the compiler freezes native
binary64 constants into normal managed objects. Integer true division rounds
exact ratios in longobject rather than narrowing both operands first.
Power/shift/bitwise dispatch uses the same ordinary/reflected/in-place protocol
boundary; complex values and the complete numeric API remain unfinished.
`Objects/representation.cpp` provides other value representation/hash
operations. `Objects/rangeobject.cpp` owns immutable arbitrary-integer ranges and
their iterators; `Objects/iterobject.cpp` owns iteration and immutable sequence
indexing. `Include/internal/long.hpp` shares the existing integer representation
and publication helper, without a second numeric implementation.
`Objects/object.cpp` contains the initial values, shared dictionary/namespace
storage, core image and common object construction. `Objects/dictobject.cpp` owns
public dict/mappingproxy methods, views and iterators; callbacks use ordinary
protocols outside storage locks. `Objects/listobject.cpp` owns mutable list storage,
constructors and protocols, with prepared capacity growth and local commits.
List displays lower through the same AST/CFG into BUILD_LIST; sys.path, regular
package paths and CLI argv use that real container, not host-only arrays.
`Objects/sliceobject.cpp` supplies traced immutable slice fields and normalization;
sequence owners perform conversions before their rooted snapshots/local commits.
`Objects/formatobject.cpp` supplies the common format/ascii protocol boundary;
f-string lowering uses it rather than an alternate string-conversion path.
Generated Unicode tables retain their generator/source provenance. The immutable
XID ranges are reused by the standalone tokenizer; its normalization and licensed
character-name tables remain native parser data, not Python objects or imports.
`Objects/moduleobject.cpp` uses the ordinary instance dictionary as the module
namespace, including descriptor lookup and module-level __getattr__. Module
payloads add no second dictionary or lifetime manager. The CLI now executes in
the actual registered __main__ namespace; RunRequest accepts a builtins module
or dictionary and binds functions to that module's actual dictionary.

`Python/gc.cpp` owns the precise heap, adjacent allocation metadata, thread-local
spans/root arenas, attachment/parking, epoch rendezvous, transfer roots and core
image lifecycle. These coupled lifetime operations remain in one translation
unit; moving them does not introduce an extra lifetime authority or new locking.

Runtime contracts are in `Include/cxxpython/`. Compiler-private implementation
structures belong in `Include/internal/`. All C++ interfaces remain unstable.
`Include/cxxpython/extension.h` specifies the experimental C11 extension ABI;
there is no CPython ABI compatibility promise.

## Parallel Execution

Several Active attachments may execute bytecode and protocols in one shared
Interpreter. Roots and active Frame state belong to each attached OS thread.
GC uses request/ack/completed epochs and scans only frozen roots. NativeWait
parks a thread without exclusive execution permission on resume.

Mutable shared references use rooted atomic edge operations. Ordinary name
dictionaries use per-object short commits, prepared host nodes and rooted views;
replacement/deletion do not require managed allocation. Type dictionaries and
metadata still publish immutable snapshots; Code and Function metadata are
immutable. Closure Cell identities are shared, while each call owns its Frame
and fresh local Cells. CodeKind distinguishes function, module and class-body
namespace rules; a class Frame owns/traces its supplied mapping and implicit
class Cell rather than putting invocation state in shared Function/Code.
There is no whole-interpreter ordinary execution lock.

## Tests And Tooling

`Tests/cpp/` tests production runtime interfaces. `Tests/python/` contains host Python
drivers for actual executable differential and measurement-report tests, not a
ported standard library. `Lib/` is reserved for the interpreter's Python standard
library. `Tools/benchmarks/runtime.cpp` runs the current runtime;
`Tools/benchmarks/record.py` retains raw runs and generates comparison tables.

The old implementation and its tests are absent, not disabled behind a build
option. Historical benchmark observations remain evidence of their recorded
commits, not claims that removed features work in the current executable.
