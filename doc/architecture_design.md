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
Completed sys.modules aliases, including os.path, are resolved before requiring
the parent to be a package; alias hits do not republish a new parent attribute.
Warnings/metadata/resources, directory caches, bytecode caches, encoding and
installed path configuration remain gates. Temporary CPython adaptations and
their closure conditions are recorded in `doc/cpython_compatibility.md`.

## Objects And Lifetime

`Objects/typeobject.cpp` owns type layout/readiness, C3 and immutable type views;
`Objects/typeconstruction.cpp` owns Python class-construction hooks. Runtime and
core types receive missing default documentation metadata at their respective
readiness/bootstrap boundaries. Descriptor __set_name__ failures preserve the
original exception and add the CPython 3.12 context note rather than wrapping it
in RuntimeError; enum is not special-cased in class creation.
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
boundary. Integer bit_length/bit_count use the existing arbitrary-precision
magnitude; bin/oct/hex use the normal index protocol at the builtin boundary.
`Objects/complexobject.cpp` owns immutable binary64 complex values,
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
`Objects/bytesmethods.cpp` shares bytes/bytearray search, partition, strip and
split algorithms, index/buffer conversion and family-correct result construction.
It consumes existing safe snapshots rather than retaining unleased raw storage
across callbacks; this is not a path-specific implementation or a claim of the
complete bytes API/mutation matrix.
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
weak reference protocols; `Objects/weakproxyobject.cpp` forwards proxy operations
through rooted referents. `Modules/_weakref.cpp` exposes that native capability.
Its _remove_dead_weakref uses a rooted dictionary binding and conditional deletion
so a dead weakref cannot remove a concurrently replaced entry. Lib/weakref.py and
Lib/_weakrefset.py retain complete upstream mapping/set, WeakMethod and finalize
policy; weak-target clearing remains tied to this tracing collector, not RC timing.
`Python/errors.cpp` owns exception/unraisable hook dispatch, including callbacks
from GC and generator close. `Modules/gcmodule.cpp` exposes collector controls.
`Objects/exceptions.cpp` owns errno-based OSError construction and traced native
error fields, including characters_written. BaseException has an ordinary traced
instance dictionary; add_note uses rooted reads and conditional first-list
publication for ordinary attributes while preserving custom attribute protocols.
`Objects/reduction.cpp` supplies object reduction/state hooks over ordinary
attributes, dictionaries, slots and iterators, using the unchanged Lib/copyreg.py
helpers. Native layouts retain explicit reconstruction restrictions; neither the
copyreg source nor these hooks provide a complete pickle implementation.
Lib/copy.py retains upstream shallow/deep-copy policy. Concrete container,
numeric, text, bytes, range and slice files provide their own reducers or exact
native constructor arguments rather than adding layout-specific cases to
Objects/reduction.cpp. Subtype state and memo graphs travel through the ordinary
Python reconstruction path; callbacks do not run under storage locks.
BoundMethod construction, equality/hash and NativeFunction __self__ metadata live
at the existing call/descriptor/representation boundaries, not in weakref or enum.
`Modules/posixmodule.cpp` supplies real descriptor, directory,
filesystem-status and process/environment primitives, with blocking host work
parked. Its stat_result is a validated immutable tuple subtype with readonly
native tail fields; native layout planning does not enable Python tuple slots.
OS environment operations share a short process-wide host mutex because the
underlying environment itself is process-wide. External embedding code must
coordinate its own concurrent environment mutations. stat/lstat dir_fd,
stat follow_symlinks/file descriptors and chdir/fchdir use actual POSIX calls;
the advertised capability list includes only implemented variants.
Lib/os.py, genericpath.py and posixpath.py retain upstream environment/path policy.
Filesystem encoding is currently UTF-8/surrogateescape, not a completed locale or
installed-runtime configuration system. Other POSIX APIs, Windows portability and
the complete os/pathlib policy/error matrices remain separate delivery work.

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
the entire stream on every append.

`Modules/_io/bufferedio.cpp` supplies BufferedReader/Writer/Random/RWPair with
actual raw-stream callbacks, input/output buffers, partial/nonblocking writes,
position reconciliation, detach and close. `Modules/_io/textio.cpp` supplies
IncrementalNewlineDecoder and TextIOWrapper, using registered incremental codecs,
pending decoded text, newline state and byte/decoder snapshots for tell/seek
replay cookies. Encoded output has its own pending buffer: write-through, line
buffering and flush/read/position/detach/close barriers preserve text-versus-binary
ordering. Pending bytes detach before callbacks, so failed writes are not silently
replayed. Managed stream/codec edges are traced. Per-object operation claims
retain stream ownership across callbacks; state mutexes protect claim transitions,
not Python execution, and contending threads park through NativeWait. Same-thread
reentrancy is rejected rather than deadlocking or acquiring an interpreter lock.

`Modules/_io/open.cpp` owns native stream-chain construction for builtins.open and
_io.open: FileIO, the appropriate buffered type and optional TextIOWrapper. Mode/
option validation and failure cleanup use normal calls and exception chaining.
The builtin resolves the real _io implementation, whose captured types retain
the construction dependencies. `Modules/_iomodule.cpp` publishes the actual types;
the complete upstream Lib/io.py supplies the Python facade and ABC registration.
`Modules/_io/stdio.cpp` constructs sys.stdin/stdout/stderr using these real types,
with borrowed FileIO descriptors and POSIX UTF-8 defaults; the temporary native
stream type is deleted. Cold import bootstrap has explicit policy and stream
phases: configured stdlib search precedes user path[0], and the installed Python
finders handle stream dependencies before ordinary callers are admitted. The
original sys dictionary has a precise Interpreter root independent of module
table replacement. Input, print, display and exception hooks resolve its live
attributes; a fresh reimported sys module does not redefine interpreter stdio.
The CLI and REPL read through those Python streams and flush after execution and
at finalization. Locale/environment configuration, signals,
warnings, TextIOWrapper.reconfigure and complete stream protocol/diagnostic
matrices remain work, not implied by the existing stream hierarchy.

`Modules/readlinemodule.cpp` supplies GNU readline when its development library
is detected. Typed module state traces completer/startup/pre-input callbacks and
owns its native history. A short process-local claim coordinates only GNU's
external, process-global terminal state; no mutex covers Python callbacks or
ordinary VM execution. History state swaps avoid copying the whole history for
each API call. Terminal sessions restore streams, callbacks, TLS and tty state;
the whole GNU call parks with NativeWait, including blocked terminal output.
NativeCallback admits only the Python hook/completion portions as Active and
refreezes roots afterward, including native exception unwind. GNU's
longjmp-based signal handling is disabled rather than crossing C++/GC scopes.
The shared stdio selector uses GNU only for saved standard-stream identities,
descriptors 0/1 and real terminals. Redirected/replaced streams retain ordinary
readline/write protocols. Full signal integration, display hooks, automatic
rlcompleter policy and the remaining curses capabilities remain dependencies.

`Modules/_opcodemodule.cpp` exposes immutable instruction, source-position,
signature and stack-effect metadata. `Lib/opcode.py` and `Lib/dis.py` implement
Python inspection/formatting policy, including ordinary tuple protocols,
recursive listing, current-offset marking and traceback selection. Offsets are
actual instruction indexes, not invented CPython wordcode bytes. Call operands
index the owning Code's call table, so code-aware Bytecode.stack_effect reads
that table; standalone effects reject those operands rather than treating them
as arity. Full frame reflection, control-region inspection and upstream dis
compatibility remain separate gates.

`Modules/atexitmodule.cpp` owns an Interpreter-rooted traced callback registry.
Prepared callback records and short local commits separate mutation from Python
equality and invocation. A per-registry recursive run claim coordinates runners;
waiters park, and callback errors use unraisable reporting. Module collection or
reload does not own the registry lifetime. `Modules/main.cpp` invokes callbacks
after its normal script/interactive execution path; upstream weakref.finalize
registers through this real atexit API. This is not complete process/interpreter
shutdown: thread joins, standard-stream flushing, final GC/module teardown and
embedding/abnormal-exit ordering remain separate contracts.

The eight complete, unchanged CPython 3.12.14 additions are Lib/io.py, os.py,
genericpath.py, posixpath.py, enum.py, weakref.py, _weakrefset.py and copyreg.py.
They preserve upstream syntax and error paths. Enum/flag policy remains in Python;
generic type, method, integer and exception dependencies belong in Objects/.
Source completeness is not full module compatibility; the adaptation ledger
records the remaining library and product acceptance boundaries.

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
metadata still publish immutable snapshots. Code and a Function's code/global/
builtin/closure identities are immutable. Function names, documentation, module,
annotations, type parameters and default argument edges use rooted atomic
publication; each call keeps its default snapshots alive while binding. Closure
Cell identities are shared, while each call owns its Frame
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
close and automatic GC close use ordinary GeneratorExit/unwind. Shutdown phases
and unrestricted cross-thread frame-local snapshots remain work; a C++ destructor
does not execute Python cleanup.
Native coroutine calls create the same suspended Frame shape with a distinct
coroutine type/code flag. GET_AWAITABLE validates the ordinary __await__ protocol;
the existing delegation and unwind paths perform suspension, send/throw/close and
return-value propagation. Await wrappers retain their coroutine owner. Coroutine
Frames use the same cross-thread Running claim and exception-state handoff as
generators, not an event loop or a serial executor. Async generators additionally
use ASYNC_YIELD_VALUE to distinguish a yielded Python value from an inner await
suspension. Objects/asyncgenobject.cpp owns asend/athrow/aclose request objects.
A per-request execution claim protects its writer-owned state; a per-generator
atomic claim spans inner await suspension and rejects competing requests without
holding a mutex. Completion, rejection and native exception unwind release the
claim. Async with retains a flag in the existing control region and lowers entry
and cleanup through GET_AWAITABLE/YIELD_FROM; no second Frame or cleanup scheduler
is introduced. Objects/aiterobject.cpp supplies aiter/anext and the traced,
execution-claimed default-bearing wrapper. Async for uses GET_AITER and FOR_AWAIT;
the latter reuses the same Frame delegate and resume inputs as YIELD_FROM,
branching only on next-step exhaustion. Async comprehensions retain ordinary
scope/closure analysis, with eager outer-iterator evaluation and implicit awaits
for asynchronous collection comprehensions. No hidden local slots or second
coroutine scheduler are added. Async-generator finalizer hooks, event-loop
shutdown and never-awaited warning policy remain explicit dependencies.

`Objects/exceptiongroup.cpp` extends the existing exception layout with message
and child slots. Native constructors, derive/subgroup/split and metadata copying
support unchanged upstream `Lib/contextlib.py`, including group suppression.
The Python library also supplies synchronous/asynchronous generators, stacks,
decorators and resource helpers. except* lowers to ordinary Except/Finally
regions, with original/results/remainder on the traced operand stack. Complete
nested diagnostic formatting and pickle support remain gates.

Structural patterns have native typed AST nodes lowered to ordinary CFG blocks,
stack operations and six explicit matching/length instructions. Captures stay on
the precise operand stack until the structural pattern succeeds, then enter the
normal name/Cell/namespace store path before guard evaluation. Deferred failure
blocks clean temporary operands; OR arms normalize binding order before joining.
No runtime AST interpreter, hidden locals dictionary or separate unwind state is
introduced. `Objects/patternmatching.cpp` implements mapping-key/rest and class
protocols with rooted callback results. Sequence/mapping classification is an
atomic semantic TypeObject field inherited through the MRO and propagated by ABC
registration, not inferred from physical layout or __getitem__ availability.

`Objects/funcobject.cpp` owns Function traversal and metadata descriptors; the VM
owns construction and call binding, not a second metadata dictionary. Independently
assigned defaults and keyword defaults are not a multi-field transaction.
Writable __code__ and Code.replace remain separate compatibility work.

Non-generic function annotations evaluate in the defining scope after defaults
and are carried by MAKE_FUNCTION's annotation flag. Their mutable dictionary edge uses
rooted atomic reads, stores and lazy compare-exchange publication; it is not a
second namespace entry with conflicting authority. Future annotations instead
use `Python/ast_unparse.cpp`, adapted from the pinned native annotation unparser,
before name mangling. Disconnected Annotation scopes validate the expression
without adding runtime closures. Unused local-annotation scopes have no emitted
graph and do not get a Code object. BoolExpr keeps an explicit operand vector,
and FormattedExpr preserves format-spec presence so canonical output does not
lose grouping or empty format specifications.

Type-alias lowering creates a lazy evaluator in an annotation scope. Generic
functions, aliases and classes additionally use a distinct AnnotationRole
type-parameter scope: defaults remain in the outer scope, while function
annotations and generic class bases use the inner scope. Bounds and constraints
use separate annotation roles. In a class the relevant scopes capture
__classdict__; construction fills that Cell with the class dictionary, and dedicated VM reads
consult it before closure or global lookup. The native _typing.TypeAliasType
traces its immutable inputs and atomically publishes the first successful cached
value without serializing evaluator calls. Static native type module metadata is
kept separately from instance descriptors named __module__.

`Objects/typevarobject.cpp` and `Objects/paramspecobject.cpp` implement native
TypeVar, ParamSpec, TypeVarTuple, ParamSpecArgs, ParamSpecKwargs and Generic
surfaces exported by `_typing`. Parameter instances have dictionary and weakref
storage, although their `__dict__` descriptor is hidden; ParamSpec views have
weakrefs but no instance dictionary. Lazy TypeVar bound and constraints
evaluators are traced, with atomic traced caches. The compiler and VM connect
MAKE_TYPE_VAR, MAKE_PARAM_SPEC, MAKE_TYPE_VAR_TUPLE, MAKE_GENERIC_BASE and
SET_FUNCTION_TYPE_PARAMS to these objects and generic definitions.

Python policy remains in `typing`: Generic classmethod hooks and generic base
creation, non-None TypeVar bound checking, substitution, unions and
TypeVarTuple unpacking call through real imports. Missing imports and errors
propagate, including for classmethod Generic policy; there is no fabricated
typing fallback. Generic class compilation is connected, but the actual
typing/re/warnings dependency closure and acceptance remain open.
Native _opcode.local_names exposes actual VM fast-local slots to dis; those
indices are not indices into the cell-filtered Python co_varnames tuple.

Variable annotations lower through ordinary stores and SETUP_ANNOTATIONS, not a
second VM evaluation path. Type annotation reflection reuses the own namespace's
immutable metadata publication, including atomic lazy initialization and normal
descriptor callbacks. Compiler future flags belong to SourceUnit/VerifiedCode
and each REPL invocation, never mutable interpreter-global compilation state.
Generic aliases and unions
hold immutable rooted argument tuples and atomically publish immutable parameter
tuples. Class subscripting prefers metaclass __getitem__, then the ordinary
__class_getitem__ protocol. NamespaceObject reuses the existing traced instance
dictionary tail, and mappingproxy delegates to the wrapped mapping's real
protocols rather than assuming that every mapping has a DictObject payload.

`Modules/itertoolsmodule.cpp`, `itertools_combinatorics.cpp` and
`itertools_groups.cpp` divide iterator algorithms by shared state shape.
`Modules/_collectionsmodule.cpp` supplies native deque, dict-backed defaultdict
and tuple fields; `Lib/collections`, `reprlib` and `functools` retain upstream
Python policy. No native copy of these complete Python libraries is introduced.
The weak sys.intern pool belongs to the Interpreter heap coordinator and is
pruned before sweep releases string storage, not kept as permanent module roots.

## Tests And Tooling

`Tests/cpp/` tests production runtime interfaces. `Tests/python/` contains host Python
drivers for actual executable differential and measurement-report tests, not a
ported standard library. `Lib/` is reserved for the interpreter's Python standard
library. Python-visible contracts use `Tests/cases/*/test.py`, with optional
`stdin.bin` for process input and exact stdout/stderr/status comparisons through
`Tests/python/test_cases.py`, without
compiling C++ wrappers. Native tests cover contracts inaccessible from Python.
`Tests/nogil_cases/*/test.py` uses the same driver for explicit shared-heap
contracts that differ from the fixed GIL reference, currently at-most-once
concurrent atexit dispatch. It is a separate CTest, not a reference-test exclusion.
`Tools/benchmarks/runtime.cpp` runs the current runtime;
`Tools/benchmarks/record.py` retains raw runs and generates comparison tables.

The old implementation and its tests are absent, not disabled behind a build
option. Historical benchmark observations remain evidence of their recorded
commits, not claims that removed features work in the current executable.
