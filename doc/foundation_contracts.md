# Current Runtime Contracts

The authoritative full objective is
[Plan 2](../cxxpython_foundation_redesign_plan_2.md). Current source organization
is documented in [architecture_design.md](architecture_design.md). The previous
contract/migration ledger is preserved in
[archive/foundation_contracts_2026-10-29.md](archive/foundation_contracts_2026-10-29.md);
it includes superseded serial and migration assumptions and is not the live API.

## Scope And Status

The only implementation is `cxxpython::core`, built as `cxxpython_core` and exposed
by `cxxpython`. Legacy code, standard library, extension API and tests were removed
at the user's request. That removal does not establish M9/N6 acceptance.

N0-N5 and M0-M6 contain experimental slices; complete language, object/protocol,
library, thread/resource, platform/install and performance acceptance remain.
M7-M9/N6 are not complete. No CPython ABI, .pyc or extension ABI is promised.

The accepted source subset includes arbitrary integer/string literals,
implemented unary/arithmetic/boolean/comparison protocols, name/attribute
operations, if/while/for control flow, calls, function definitions/defaults/variadic
binding, globals/nonlocal, closures, docstrings, tuple expressions, recursive
non-starred unpacking and integer/protocol subscripts. Experimental range,
tuple/string iteration, iter(obj), next(it[, default]) and exception construction
use ordinary object protocols. Mutable containers, slices, full iteration APIs,
class and exception source lowering, imports, suspension, f-strings, float/power
protocols, full Unicode source handling and the remaining Python 3.12 matrix are
unfinished. These are remaining delivery requirements, not a reduction of the
basic-language objective. Existing native VM/type/cleanup interfaces do not imply
source or library support.

## Identity And Lifetime

Value is a non-owning, non-null boxed identity. Python inheritance and immutable
native allocation layout are distinct; payload access validates layout/domain.
The nonmoving precise tracing collector is the sole lifetime authority. Value
copies do not adjust Python reference counts. Constructors may allocate host
storage but cannot run Python, collect or publish partial objects.

RootScope owns stable cells in the attachment's thread-private arena, reusing
each cell's existing link for scope membership/free storage. Explicit non-LIFO
release unlinks before reuse. Handle validates interpreter, arena and generation.
No root operation may be used from another attachment/thread or during NativeWait.

ReturnSlot is a noncopyable caller-owned stable root. Empty, Ok and Raised are
distinct; callees publish before dropping their roots. Immediate accessors read
the current cell after owner/tag checks. Saved ResultViews retain revision and
generation checks and reject overwrite/destruction/reuse. Aliased inputs are
captured before resetting outputs. Native critical publication uses only prepared
roots and bounded no-allocation operations; NoSafepoint is not an execution lock.

## Threads And GC

One Interpreter permits multiple Active OS-thread attachments executing Python
bytecode and object protocols in one heap. An attachment owns roots, active
Frames, span allocation/pressure batches and protocol caches. Normal access does
not consult a mutable global heap map or a whole-interpreter execution mutex.

GC coordinates request/ack/completed epochs; attach, retirement and NativeWait
resume share the epoch gate. Only acknowledged frozen roots are scanned. VM
dispatch polls periodically, including allocation-free loops. NativeWait freezes
roots but grants no detached payload access and does not serialize resume.
Blocking host operations must release heap access through that boundary.

Transfer is a move-only delivery lease backed by a managed, collector-visible
record. Receive roots the payload before unlinking delivery ownership. Handle
and ReturnSlot are never transferred directly between threads. Closing rejects
new admission while existing leases can finish; bootstrap publication alone
requires single-initializer admission.

## Shared Fields And Calls

Header/type references, instance field tails, Cell contents and exception graph
edges use atomic rooted operations. Name Dict storage and Type metadata publish
immutable snapshots, prepared outside short commits. Callbacks, allocation and
blocking do not occur under internal publication critical sections.

Code and Function/default/closure identities are immutable; dictionaries and
referenced values retain their own synchronization semantics. Per-thread protocol
caches guard on Type identity, typed protocol and matching publication epoch.
They hold borrowed identities, are cleared before sweep/address reuse and root
entries before a safepoint. Shared Code does not contain mutable execution caches.

All call shapes use the same binder and caller-owned result boundary. Complete
positional native calls borrow spans or prefix a receiver in bounded stack
storage, with a vector for larger arities. Defaults, keyword-only/variadic cases,
recursion guards, callback roots and return validation remain intact.

## Code, Frames And Closures

CodeBuilder verifies opcode domains, bounds, stack joins, call shapes, control
regions and source positions before immutable publication, including unreachable
instructions. Shared Code has actual managed name/constant/metadata tuples.
Private bytecode encoding is not a CPython compatibility promise.

Frame state belongs to one invocation/attachment. Its operand storage is fixed
before execution. The scheduler transfers returns into parent traced storage
before releasing the child; ordinary Python calls are non-recursive in C++.
FrameActivation borrows nonmoving Frame/Code payloads under its existing active
root and refreshes them only on frame switches. Polls and atomic instruction
publication for reflection remain active. Cleanup reasons and traceback graphs
are traced, not hidden in native stack state.

Code declares disjoint cell/free layouts; FAST access to captured locals is
rejected. Frame preparation roots fresh local Cells and existing free Cells
before publishing its const cell-identity vector. Captured parameters have no
second mutable fast-local authority. Siblings share Cells; distinct factory calls
do not. Function/Cell cycles use ordinary tracing, not reference counting.

LOAD_DEREF performs one rooted atomic read; STORE_DEREF stores the still-traced
input; DELETE_DEREF performs one exchange, not check-then-clear. Empty local Cells
raise UnboundLocalError, free Cells raise NameError. LOAD_CLOSURE captures Cell
identity even when unbound. MAKE_FUNCTION consumes defaults, kwdefaults, closure
tuple and Code in that order when flags 1/2/4 are present. Shape errors use Python
TypeError/ValueError. co_varnames, co_cellvars, co_freevars and __closure__ expose
actual managed metadata; runtime closure creation reuses the scope's Code.

Several threads may call the same Function/Code/closure. Individual shared Cell
edges are synchronized; compound nonlocal += is not implicitly atomic.

## Iteration And Items

get_iter resolves __iter__ on the type and checks the result's __next__ slot.
iter_next distinguishes Item, Done and Raised; Done retains the original
StopIteration (including subclasses) in the caller's exception slot. Other
exceptions are not exhaustion. None is a valid yielded/default value, not a
termination sentinel. The __getitem__-only sequence fallback and two-argument
iter(callable, sentinel) remain unimplemented.

Range stores immutable normalized start/stop/step and arbitrary-integer length.
It rejects zero step, reports len overflow separately, indexes without narrowing
big integers and compares/hashes sequence content. A range iterator atomically
claims its rooted current value only after preparing the successor. Tuple and
Unicode string iterators share an atomic owner/index layout and claim an index
only after preparing its result. Concurrent next calls return each item once;
no interpreter lock or Python callback is held in the claim. Exhaustion is sticky.
Allocating a result/successor unsuccessfully does not consume the current item.
Integer indexing follows __index__; negative indices are normalized, and invalid
indices raise IndexError. Range slicing, reverse/membership helpers and the full
container method matrices remain gates.

FOR_ITER has distinct taken/fallthrough stack effects, both checked by the Code
verifier. Loop exhaustion removes the iterator before else; break removes it
without executing else; continue preserves it. UNPACK_SEQUENCE collects and roots
the required values, probes for surplus and only then exposes target stores.
Subscript augmented assignment evaluates owner/key once, using the same typed
getitem/setitem dispatch as the public runtime API. List/dict syntax and starred
targets are not approximated by tuple behavior.

BaseException construction uses the native exception layout and a managed args
tuple; args replacement consumes an iterable before atomically publishing its
tuple. StopIteration.value is an independent traced atomic tail field: assigning
args or calling BaseException.__init__ does not overwrite it, while calling
StopIteration.__init__ resets it. This is not a claim of complete exception
subtype-specific constructors or source try/raise support.

## Frontend And Measurement

SourceUnit owns immutable native Source/ScopeInfo/CFG, contains no hidden Python
identities and can be shared by parallel compilation tasks. Attached freeze roots
all managed materialization. RunRequest and the single CLI use this same service,
not a second language path. Native SourceError preserves source spans and category;
runtime faults use Raised and real tracebacks. Globals/builtins are actual name
dictionaries; arbitrary namespace mappings and Python compile/exec/eval are absent.

Native contracts live in Tests/cpp; actual executable differential drivers live
in Lib/test and use fixed CPython 3.12.14 when configured. Benchmark source kernels
are captured verbatim for the paired reference; raw runs retain version/build/hash
provenance and generate tables. Same-Function source, closure and for-loop probes
exercise CPU parallelism, but absolute single-thread CPython parity remains a
failed gate.
Do not replace that requirement with multicore scaling or a native-only probe.
