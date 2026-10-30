# Current Architecture

The repository follows CPython's directory responsibilities, not its object ABI
or private bytecode format. This document describes only the current runtime.
Historical designs are in `archive/`; Plan 2 remains the full delivery objective.

## Source To Execution

`Programs/cxxpython.cpp` is the thin process entry. `Modules/main.cpp` owns host
arguments, source selection and interpreter setup. `Python/pythonrun.cpp` owns
interactive driving and diagnostics; `Modules/sysmodule.cpp` supplies native
displayhook/exit capabilities. Scripts, stdin, -c, -m and REPL enter through the
same SourceUnit/RunRequest, not a second parser or VM. The actual module/package
execution policy is adapted Python code in `Lib/runpy.py`. There is one executable,
`cxxpython`, linked to `cxxpython_core`.

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
`Objects/genobject.cpp` owns generator protocols and execution claims. Generator
functions bind/create an ordinary Frame without executing its body; yield and
yield-from suspend that same Frame/operand stack/cleanup state. Resumption is
still ceval, not another interpreter or source execution path.
`Python/bltinmodule.cpp` owns the ordinary __build_class__ builtin, coordinating
base expansion, metaclass preparation, a normal class-body Frame and existing
type construction. It is not a separate class VM or lifetime authority.
`Python/import.cpp` owns the experimental registered/file-source/static-native
initialization transaction and bootstrap coordination. Host-only per-Interpreter
records coordinate owners/waiters; the managed module table is the only completed
module authority. Python policy snapshots visible search paths; file reads and
compilation park at native boundaries. Source import statements lower through the existing
scope/CFG passes into verified import opcodes and normal calls/unwinding.
`Python/extension.cpp` bridges the versioned C API and POSIX dynamic libraries into
that same transaction. `Modules/_impmodule.cpp` supplies source/code execution and
loader callbacks; `Lib/importlib/` defines ModuleSpec, loader classes and Python
import/reload policy. Cold bootstrap alone coordinates one owner; after boot,
unrelated module bodies retain independent leases and execute concurrently.
Seed modules and Python bootstrap use the same publication/rollback transaction;
no native cache duplicates Python classes or module namespaces. Reload preserves
the existing module/dictionary and does not remove it on body failure. Modern
finder/loader and path-hook routing, including dynamically refreshed namespace
paths, live in Python; the old native file-search policy has been deleted.
Warnings/metadata/resources, directory caches, bytecode caches, encoding and
installed path configuration remain gates. Temporary CPython adaptations and
their closure conditions are recorded in `doc/cpython_compatibility.md`.

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
boundary. `Objects/complexobject.cpp` owns immutable binary64 complex values,
construction and mixed arithmetic. Imaginary literals freeze those same values;
complex formatting shares the numeric format parser rather than a second one.
`Objects/representation.cpp` provides other value representation/hash
operations. `Objects/rangeobject.cpp` owns immutable arbitrary-integer ranges and
their iterators; `Objects/iterobject.cpp` owns iteration and immutable sequence
indexing. `Include/internal/long.hpp` shares the existing integer representation
and publication helper, without a second numeric implementation.
`Python/pylifecycle.cpp` builds the initial core image and installs its native
types/builtins. `Objects/object.cpp` contains common value construction and layout
tracing, not the core initializer or a second hash table implementation.
`Objects/dictstorage.cpp` owns shared dictionary/namespace storage, prepared-node
commits, rooted snapshots and iterator cursor operations. `Objects/dictobject.cpp`
owns public dict/mappingproxy methods and protocol registration; callbacks use
ordinary protocols outside storage locks. `Objects/setobject.cpp` owns set and
frozenset construction, representation/hash, lookup and element mutation;
`Objects/setoperations.cpp` owns their bulk algebra and method/operator bindings.
Sets reuse a private dictionary table and its existing synchronization, rather
than a second storage or lifetime authority. `Objects/listobject.cpp` owns mutable list storage,
constructors and protocols, with prepared capacity growth and local commits.
List displays lower through the same AST/CFG into BUILD_LIST; sys.path, regular
package paths and CLI argv use that real container, not host-only arrays.
Stable list sorting detaches a precisely rooted input and runs key/comparison
callbacks without the list lock. Mutation epochs detect intervening writes;
prepared short commits restore the list on failure. `sorted` uses the same
implementation. `Objects/bytearrayobject.cpp` owns mutable byte sequences with
short versioned commits and rooted iterator reads. Scalar reads/writes and tail
pop do not copy the whole buffer; append uses prepared amortized growth.
`Objects/memoryobject.cpp` supplies aliasing byte-oriented memoryviews. Each live
view traces its exporter and shares a native export token retaining the actual
bytearray storage. The storage's export count forbids resizing, not same-size
writes. Release atomically drops both the traced edge and native token; native
token destruction never accesses a potentially dead managed exporter. Buffer
consumers copy bytes under short storage locks before callbacks or native waits.
`Objects/enumobject.cpp` owns reversed/list-reverse iterators, enumerate, zip and
map. Child iterators/callables are traced; ordinary protocol callbacks do not run
under storage locks. Compound callback sequences are not advertised as atomic.
`Objects/sliceobject.cpp` supplies traced immutable slice fields and normalization;
sequence owners perform conversions before their rooted snapshots/local commits.
`Objects/formatobject.cpp` supplies the common format/ascii protocol boundary;
f-string lowering uses it rather than an alternate string-conversion path.
`Objects/stringformat.cpp` parses percent/brace interpolation and dispatches
conversions and fields through those ordinary protocols. `Objects/unicodeobject.cpp`
owns the added Unicode prefix/suffix methods; codec kernels remain separate in
`Objects/unicodecodecs.cpp`.
Generated Unicode tables retain their generator/source provenance. The immutable
XID ranges are reused by the standalone tokenizer; its normalization and licensed
character-name tables remain native parser data, not Python objects or imports.
Unicode properties/case mappings use the pinned CPython Unicode 15 database;
namereplace reuses the existing compressed character-name data, not a second table.
`Modules/_codecsmodule.cpp` owns a per-Interpreter traced codec registry with
short local commits and generation-checked cache publication. Search functions
and error handlers run through normal calls outside registry locks; lazy import
loads the real `Lib/encodings` policy. `Objects/unicodecodecs.cpp` owns UTF,
ASCII, Latin-1 and charmap kernels, streaming boundaries and error dispatch.
`Lib/codecs.py` retains upstream incremental and stream policy.
`Modules/_struct.cpp` owns immutable compiled binary layouts and synchronized
buffer operations; `Lib/struct.py` remains the upstream facade.
`Modules/_hashopenssl.cpp` supplies OpenSSL EVP hashes, PBKDF2 and scrypt, plus
portable upstream BLAKE2 kernels in `Modules/_blake2/impl`. Hash objects use
per-state native locks, with attachments parked during native work. Digest/copy
snapshots do not consume the original state. `Lib/hashlib.py` retains upstream
algorithm selection and policy. These modules add no interpreter execution lock.
`Objects/moduleobject.cpp` uses the ordinary instance dictionary as the module
namespace, including descriptor lookup and module-level __getattr__. Module
payloads add no second dictionary or lifetime manager. The CLI now executes in
the actual registered __main__ namespace; RunRequest accepts a builtins module
or dictionary and binds functions to that module's actual dictionary.

`Python/gc.cpp` owns the precise heap, adjacent allocation metadata, thread-local
spans/root arenas, attachment/parking, epoch rendezvous, transfer roots and core
image lifecycle. These coupled lifetime operations remain in one translation
unit; moving them does not introduce an extra lifetime authority or new locking.
Weak targets and intrusive callback/finalizer batches are classified during STW;
Python runs only after attachments resume. Pending and running batches retain
their complete strong graphs, and a second mark checks resurrection. Immutable
type/MRO snapshots supply instance finalizers. `Objects/weakrefobject.cpp` owns
weak reference protocols, while `Modules/_weakref.cpp` exposes native capability.
`Python/errors.cpp` owns exception/unraisable hook dispatch, including callbacks
from GC and generator close. `Modules/gcmodule.cpp` exposes collector controls.
`Objects/exceptions.cpp` owns errno-based OSError construction and traced native
error fields. `Modules/posixmodule.cpp` supplies real descriptor, directory,
filesystem-status and process/environment primitives, with blocking host work
parked. Its stat_result is a validated immutable tuple subtype with readonly
native tail fields; native layout planning does not enable Python tuple slots.
OS environment operations share a short process-wide host mutex because the
underlying environment itself is process-wide. External embedding code must
coordinate its own concurrent environment mutations. Full os/_io policy and
resource-owning file objects remain separate library delivery work.

`Modules/_io/` now separates stream responsibilities. `iobase.cpp` supplies
ordinary context/iteration/line and raw-stream defaults; `fileio.cpp` owns POSIX
descriptors, and `bytesio.cpp`/`stringio.cpp` own native memory streams. A FileIO
operation borrows a descriptor under a short local commit, then parks for the
syscall without that lock. Close waits for in-flight operations before releasing
the old descriptor, preventing reuse while a read/write still uses it. Flush
callbacks run outside locks, and their failure still closes owned resources.
Native writable-buffer exports root their exporter and release the export at
scope exit, including exceptional exits, instead of creating GC-delayed views.
Memory-stream capacity is prepared outside locks; geometric growth avoids copying
the entire stream on every append. `Modules/_iomodule.cpp` exposes these actual
capabilities. The former bootstrap stdio bridge moved unchanged to
`Modules/_io/stdio.cpp`; buffered/text streams and the complete `Lib/io.py` closure
are not replaced by a fake Python facade.

`Modules/_abc.cpp` backs the unmodified pinned `Lib/abc.py`. Per-module invalidation
epochs and traced weak sets supply registry/positive/negative caches; Python
hooks and metaclass hash/equality run without internal locks. Negative caches
publish an epoch and set together so an in-flight old result cannot invalidate
new registration. Direct type subclasses use ordinary prepared dictionary nodes,
weak references and allocation-free conditional deletion, not an ever-growing
tuple or strong references to user classes. Core promotion records this graph in
its rollback transaction. Abstract flags publish with the class namespace in
TypeSnapshot. Python class/static wrappers publish their callable with a rooted
atomic edge; reinitialization and inherited abstract markers use ordinary
protocols. Native image wrappers retain a read-only callable, so wrapper
reinitialization cannot bypass the owning builtin type's immutability.

`Modules/_threadmodule.cpp` supplies native locks and low-level OS thread entry.
Transfer already owns a shared-heap lease and precise delivery root; a new
Attachment may use that lease without borrowing an Interpreter C++ object. The
same admission gate rejects launches after close. A successful start is returned
only after child admission, and admitted children retain heap state independently
of the embedding owner's lifetime. Lock waits park; ordinary Python runs on the
existing parallel VM. Uncaught Python exceptions use unraisable reporting and
SystemExit exits quietly. These low-level daemon threads are not the unfinished
`threading` join/shutdown policy.

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

Generator Frames have Created/Running/Suspended/Completed states. A resumer
atomically claims Created or Suspended; concurrent/reentrant send/throw/close
reject Running rather than waiting for the function body. The VM detaches the
Frame and restores caller exception state before the owner release-publishes a
suspension. The next owner acquire-claims it, including on another OS thread.
Borrowed caller exception state is detached at suspension; generator-owned
handlers and cleanup continuations persist. Public back/delegate edges are
rooted atomic reads, not unprotected reads of live operand storage. Explicit
close and automatic GC close use ordinary GeneratorExit/unwind. Coroutine/async
execution, shutdown phases and unrestricted cross-thread frame-local snapshots
remain work; a C++ destructor does not execute Python cleanup.
Native coroutine calls create the same suspended Frame shape with a distinct
coroutine type/code flag. GET_AWAITABLE validates the ordinary __await__ protocol;
the existing delegation and unwind paths perform suspension, send/throw/close and
return-value propagation. Await wrappers retain their coroutine owner. Coroutine
Frames use the same cross-thread Running claim and exception-state handoff as
generators, not an event loop or a serial executor. Async generators/async
for/async with and never-awaited warning policy remain explicit dependencies.

## Tests And Tooling

`Tests/cpp/` tests production runtime interfaces. `Tests/python/` contains host Python
drivers for actual executable differential and measurement-report tests, not a
ported standard library. `Lib/` is reserved for the interpreter's Python standard
library. Python-visible contracts default to `Tests/cases/*/stdin.bin` with exact
stdout/stderr/status comparisons through `Tests/python/test_cases.py`, without
compiling C++ wrappers. Native tests cover contracts inaccessible from Python.
`Tools/benchmarks/runtime.cpp` runs the current runtime;
`Tools/benchmarks/record.py` retains raw runs and generates comparison tables.

The old implementation and its tests are absent, not disabled behind a build
option. Historical benchmark observations remain evidence of their recorded
commits, not claims that removed features work in the current executable.
