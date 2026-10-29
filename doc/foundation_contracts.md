# Foundation Contracts And Migration Ledger

## Authority And Status

The repository-root `cxxpython_foundation_redesign_plan_2.md` is authoritative.
It supersedes the first plan's per-interpreter execution-lock target. This ledger
records implementation evidence, not a replacement architecture. Earlier slices
below describe their then-serialized implementation and do not establish no-GIL
compatibility. Older RC/no-GIL documents concern the **legacy** implementation,
not another lifetime authority for this tracing core.

The current priority is N0 -> N1 -> N2 -> N3: contracts, parallel roots/heap/GC,
shared-object synchronization, then the existing VM's shared-Function/Code CPU
parallelism. Multiple Active mutators in one Interpreter/shared heap are required.
I/O detachment, separate interpreters and native computation do not substitute
for simultaneous Python bytecode and ObjectOps execution. Remaining M0-M9 work
is retained; the concurrency gates do not narrow language/library/release scope.

| Concurrency Gate | Current Status | Remaining Evidence |
| --- | --- | --- |
| N0 access/publication contracts | Experimental | Plan 2 adopted; per-field migration contracts and implementation audits still required |
| N1 roots/heap/GC | Experimental | Thread-owned roots, multi-Active storage, request/ack/completed gate and frozen registry scanning exist; transfer records, bounded rooted shared reads and scalable heap metadata remain |
| N2 shared objects/methods | Planned | TypeSnapshot/FunctionCallState, atomic/shared fields and callback-safe critical sections |
| N3 parallel VM | Planned | Dispatch polling exists; shared same Function/Code CPU progress, allocation-free VM loop acknowledgement and 1/2/4-worker Python costs remain |
| N4-N6 completion/release | Planned | Remaining runtime, frontend/library, platform, performance and replacement gates |

State::execution and whole-Attachment execution permission have been removed.
Unbootstrapped storage permits multiple Active mutators. Core bootstrap requires
one registered thread and quarantines unconverted ObjectOps: a second Active
admission is explicitly rejected; NativeWait return waits for exclusive admission.
This temporary boundary is NOT N2 completion or Python CPU-parallel support.
The heap/layout map still has a short central metadata mutex; task 04 must
replace its hot-path lookups and accounting before N3 performance acceptance.

Baseline: Python 3.12 language semantics, with CPython 3.12.14 as the intended
fixed differential reference. No CPython ABI, `.pyc` or private bytecode promise.
The new target is `cxxpython_foundation`, namespace `cxxpython::core`. It links
only the platform threading library, not the legacy Runtime/Object/VM/parser.
All new C++ interfaces are internal and unstable until the M1-M4 gates pass.

| Milestone | Status | Remaining acceptance work |
| --- | --- | --- |
| M0 contracts | Experimental | Type/protocol/binder, execution, import and release ADR details remain to be exercised by their implementations |
| M1 storage/environment | Experimental | B0-B2 storage and seed gates now pass; interfaces remain internal/unstable |
| M2 types/methods/call | Experimental | Calls/construction, field tails, cells, B2 promotion and common hash/repr/str/truth/comparison/arithmetic dispatch exist; full protocols/reflection/mutation and weakrefs remain |
| M3 builtins | Experimental | Int/bool seed, initial Unicode value/representation/hash, tuple and cell value semantics; complete numeric, container, iterator, codec/buffer and finalization matrices remain |
| M4 execution | Experimental | Verified Code/Function/Frame, non-recursive calls, tracebacks, handler/finally/loop unwind and synchronous with exist; full exception normalization, closure/namespace execution and suspension remain |
| M5 frontend | Planned | ScopeInfo/CFG, source positions, validated Code, L0-L2 differential cases |
| M6 boot/platform | Planned | RunRequest, loader/import, CLI/embedding, I/O, thread API and shutdown |
| M7 libraries | Planned | Real workflows and dependency closures, including unittest |
| M8 optimization | Planned | Guarded optimizations and expanded language/library matrix |
| M9 replacement | Planned | Compatibility, performance, installation and retirement gates |

No Python-language feature is Supported on the new target yet. In particular,
f-strings, bare raise/chaining, unpacking, global/nonlocal and code type identity
remain requirements for M4-M5, not features inferred from legacy tests.

## Decisions Implemented In The First M1 Slice (Historical)

The ownership/layout decisions persist, but the serial permission model below
describes that first slice; the current epoch contract is at the end of this file.
These resolve the storage portion of ADR-001/002/003/006/007/010. Other parts of
those ADRs remain open; no full M0 completion claim is made.

- `Value` is a non-owning, non-null boxed object identity. Empty root/result
  state is separate from a Python value. It is not Python `None`.
- `ObjectHeader` contains only a type edge. M1 synthetic nodes can have an
  empty type edge; they are not Python-visible objects. B2 now closes the
  object/type seed cycle; B3/ObjectOps still precede Python-visible values.
- Native payloads are standard-layout C++ structs with `ObjectHeader` as their
  first member. `LayoutDescriptor::of<T>` supplies size, alignment, trace and
  native payload destroy plus an instance-field plan. Typed allocation reserves
  the complete aligned prefix/tail storage, constructs the C++ prefix and a
  separate optional-Value array, and publishes only after both are initialized.
  The heap copies this immutable description into allocation
  metadata. It never looks up destroy through an already collected Type.
- Layout access checks the native family independently of Python inheritance.
  Payload constructors may allocate host storage but must not call Python,
  mutate roots, collect, or publish partially initialized objects.
- Allocation collects **before** construction, constructs fully, registers
  storage, writes `Ok` into the caller's rooted ReturnSlot, then releases host
  ownership. A constructor/registration failure leaves no half-published heap
  object. Host faults such as `bad_alloc` remain host faults in M1; M2's actual
  MemoryError boundary must not fake them as Python ValueError.
- There is no RC, generation policy, finalizer or hidden active VM. All objects,
  including leaves, participate in byte pressure and full mark-sweep. A trace
  visits the header's type edge and then payload edges, without callbacks or
  allocation. A physical destructor only releases host resources: it must not
  follow edges into other Python objects or run Python.
- `GcEdge` writes pass through the attachment's `store`/`clear`. The first slice
  supports inline native edges plus the layout-planned traced tail array. External
  buffer capacity is explicitly reported with `account_external`; mutation is
  NoSafepoint and the next allocation observes its pressure.
- Root cells have stable addresses in a deque and are reused through a free
  list. Handle identity includes interpreter and cell generation. Non-LIFO
  release neither shifts another cell nor makes an old handle valid again.
  Released scope entries are removed so an enduring scope does not accumulate
  a token per historical allocation.
- ReturnSlot uses the same root arena. `Empty`, `Ok`, `Raised` are distinct;
  storage-only M1 tests may protect synthetic Raised payloads. After B3 bootstrap,
  Raised requires a real BaseException instance. Every write/reset changes the result revision; expired views and
  recycled cells are rejected. A result is transferred before callee roots die.
  Views/handles do not own the runtime and cannot outlive their attachment lease.
- Each interpreter owns one execution mutex, heap, root arena and policy.
  Attachment holds execution permission and the owning OS-thread identity.
  Nested calls reuse that attachment, not recursively acquire another one.
  Access from the wrong thread or while detached is an internal contract error.
- `NativeWait` freezes roots and releases execution permission; resumption
  reacquires permission before heap access. Host code must copy/lease resources
  before detaching; keeping a root is not permission to read payloads detached.
- Attachment holds a shared host lifetime lease. `close` rejects new admissions;
  existing attachments may resume and finish. The final lease destroys storage.
  Full thread registry, daemon handling, deadline/Busy shutdown and library-code
  leases are still M6 requirements, not implemented Python thread semantics.

The mutex makes GC possible while another thread is in NativeWait, without the
legacy workaround that silently skips sweeping when other frames exist. Separate
interpreters have separate permissions. Root registration/publication and edge
writes never initiate Python GC.

## Focused Verification

`Tests/cpp/core/test_memory.cpp` uses synthetic storage layouts only. It tests
production allocation, marking, root, result and attachment operations; it does
not implement a parallel test-only collector or method dispatcher.

The suite covers non-LIFO roots, cell growth/reuse, stale handles/views, header
and payload cycles, return/raised payload transfer, alignment/layout rejection,
constructor failure, forced GC before every allocation, leaf and external-buffer
pressure, explicit collection while automatic GC is disabled, bounded historical
root/heap storage, interpreter isolation, cross-thread access, NativeWait and
closing/resumption. Python descriptor and exception semantic tests belong to M2.

Build without the legacy runtime or network test dependencies:

Install Boost headers (1.74 or newer) through the host's dependency management,
or set `BOOST_MULTIPRECISION_INCLUDE_DIR` to an existing header tree. They are
used privately by the integer implementation; normal configuration never fetches
Boost or links a Boost runtime library.

```sh
cmake -S . -B /tmp/foundation-only -DBUILD_TESTING=OFF -DCXXPYTHON_BUILD_LEGACY=OFF
cmake --build /tmp/foundation-only
```

With tests, use an installed GTest CMake package or pass an existing pinned
GoogleTest 1.14.0 source tree as `FETCHCONTENT_SOURCE_DIR_GOOGLETEST`. Only when
tests are requested and no package/source exists does CMake fetch the pinned
archive. Use `ctest --test-dir <build> --output-on-failure`; each new contract
test has a 15-second timeout. ASan/UBSan and TSan use separate builds of this
same target and tests, not the old object code.

## Cost Baseline And Fixed Workloads

M1's opt-in `bench_foundation` (`CXXPYTHON_BUILD_BENCHMARKS=ON`) records 100000
synthetic node allocations with automatic GC and 100000 root register/releases,
header/payload sizes, collection count, live bytes, root-cell count and last pause.
It validates an empty final live set and bounded root storage. Allocation timing
includes result-slot maintenance and GC. Metadata bytes count Allocation and
RootCell records only, **not** STL node/bucket/deque allocator overhead or RSS.
These are initial costs, not a CPython performance result or final allocator.

Freeze the following workload categories now; fill in executable source fixtures
as each dependency lands, retaining slow cases rather than dropping them:

| Workload | Availability gate | Correctness check |
| --- | --- | --- |
| allocate/root/collect and fixed live set | M1 | Exact survivor count, reclaimed bytes, bounded metadata; record RSS separately |
| native call/bind and descriptor | M2 | Result, exception, receiver and side-effect order |
| integer/mixed numeric and Unicode | M3 | Exact values, equality/hash, codepoint sequence |
| list/dict scaling and recursion | M3 | Order, lookup, recursion behavior; linear whole iteration |
| VM calls, handler/finally/resume | M4 | Values and ordered cleanup events |
| cold start, arithmetic, methods, attributes | M5-M6 | Same source/result on pinned CPython |
| import and real library workflow | M6-M7 | Module identity, output, error and state changes |
| resource I/O concurrency | M6 | Actual bytes, close/error behavior, concurrency progress |
| `test2.py` single/multi 1000000 | M5-M6 | Prime count 78498, original explicit-GC scenario retained |

Record compiler/build, Python version, machine/CPU policy, inputs and raw repeated
measurements. Separate cold/warm, single/shared-interpreter/separate-interpreter
results and wall/internal timing. Target performance remains the plan's
geometric-mean ratio <= 1.0 with individual slow cases disclosed, plus steady
memory and pause distributions. No such claim is supported yet.

## Next Work

The active long-term goal covers M0-M9, not only the current test suite. The
current checkpoint includes native calls, construction, fields, cells, B2
promotion and common hash/repr/str/truth/comparison/arithmetic dispatch. The M3
seed supplies actual values required to exercise those protocols; this dependency
overlap does not close either milestone.
M4 now has verified Code/CodeBuilder, stable rooted Frame storage and
constant/local/arithmetic/comparison/branch/call/return execution through ObjectOps.
Ordinary Python calls now use a Frame loop. The next execution slice is
exception-normalization completion, typed suspension outcomes and
closure/namespace execution. Structured region unwind and per-Frame handled
exception restoration now run through the same scheduler as ordinary calls.
The plan explicitly permits M3 expansion and M4 execution in parallel after a
minimal builtin set exists. Full M3 is not an added prerequisite for this first
executor. Remaining type/reflection/mutation, callable value semantics,
weakref/finalization, containers, numeric/codec and ownership gates stay open;
they are not waived by starting M4.
Do not add old-VM fastpaths, new stdlib stubs or an
alternative ownership layer to bypass these dependencies.

## B2 And Structural TypeReady Checkpoint

- `Attachment::bootstrap()` is explicit and idempotent per interpreter. It
  suppresses automatic collection only for the bounded seed transaction, roots
  intermediate shells in ordinary ReturnSlots/RootScope, and publishes both
  interpreter roots after all edges, MROs and host buffers are initialized.
- `object` is an instance of `type`; `type` is its own instance and derives from
  object. Their MROs are `(object,)` and `(type, object)`. Interpreter-owned seed
  roots are scanned by the same collector as temporary roots and heap edges.
- Failure unwinds temporary roots, restores the automatic-GC setting, collects
  abandoned shells and leaves no published seeds. A managed-allocation quota
  injects failure at each of the two object allocations; the same attachment can
  retry after restoring its quota. C++ construction/metadata allocation retains
  normal RAII rollback. No claim of exhaustive host allocator fault injection.
- `TypeObject` owns its name/qualname, bases, C3 MRO, solid base and
  instance-layout description. Value arrays are built in the no-safepoint native
  construction path, traced without copying, and not mutated after publication.
  Host buffer capacities are included in GC pressure, excluding inline strings.
- `create_type` merges complete base MROs, rejects duplicate/inconsistent bases,
  and selects the most-derived compatible metaclass. `create_native_type` uses
  the same path with an explicit storage description. Solid-base ancestry, not
  merely matching C++ layout IDs, decides whether multiple bases can coexist.
- Typed allocation verifies the selected type's instance layout, writes its
  type edge before registration/publication, and returns only through the
  caller's rooted slot. It does not cast by Python subtype or expose a briefly
  untyped instance. Foreign caller slots are rejected before changing them.
- The original structural-only flag is now `TypeState::Structural`, distinct
  from Building and Ready. It means inheritance/layout metadata is complete. These
  APIs are host-side structural definitions, not `build_class`; invalid
  definitions raise the typed host `TypeDefinitionError` (an `invalid_argument`
  subtype). The later Python `type.__new__` adapter translates this specific
  fault into Raised, without catching unrelated host argument/invariant errors.
  Actual Python exceptions, type dicts, callable metaclasses at the class-build
  boundary and complete descriptor/protocol readiness still
  belong to M2/B3. They are not stubbed with approximate Python values.

`Tests/cpp/core/test_types.cpp` covers the two seed cycles, persistent and unique
type identity, idempotency, failure/retry and GC-setting restoration, concurrent
sequential cross-attachment seed identity, C3 diamonds and conflicts, metaclass selection, native
layout inheritance/conflicts, rejected unready definitions, failed caller output,
and fixed-live-set type/instance collection under forced GC. This extends the
storage tests using production operations, not a separate test object kernel.

## B3 Native Method-Call Checkpoint

`bootstrap_core()` extends B2 with managed None/Unicode/tuple/name-dictionary,
exception hierarchy, native function, native method descriptor and bound method
types. It uses caller-owned slots for the staged image, publishes interpreter
roots only after initialization, and restores the old B2 namespace edges on
failure. Each managed-allocation failure point is exercised through the actual
quota path and followed by a successful retry. Core types are frozen after boot;
normal heap type namespaces remain mutable through ObjectOps.

The first complete production flow is:

```text
create TypeSpec -> allocate typed instance -> get_attribute
  -> full-MRO namespace lookup -> descriptor binding
  -> call -> SignatureLayout binder -> NativeEntry
  -> caller ReturnSlot Ok or Raised -> collection
```

- Type and instance namespaces are actual managed DictObjects. The current
  name-only storage accepts Unicode names, keeps insertion order (including
  `**kwargs`), traces actual key/value objects, and hashes native codepoint keys
  without callbacks. This is not yet the general Python mapping protocol.
  Deletion currently shifts live entries; M3 must supply its general container
  complexity/reentrancy matrix before dict is declared Supported.
- StrObject stores codepoints, including surrogates, not UTF-8 byte positions.
  Codec, compact storage, repr/hash/indexing and the public string protocol are
  M3 work. Tuple currently provides immutable traced item storage to calls and
  exception args; its public sequence protocol is likewise not claimed yet.
- Attribute lookup separates class/instance lookup from descriptor binding and
  calling. Data descriptors precede instance storage; non-data descriptors can
  be shadowed. Type lookup honors metaclass data-descriptor precedence.
  `__getattribute__`, `__getattr__`, `__setattr__` and `__delattr__` take the same
  call path. Only AttributeError triggers the getattr fallback.
- Descriptor capabilities are looked up on the descriptor's type through its
  complete MRO. Native-function binding is a guarded exact-type shortcut; an
  independent native layout does not confer Python function/string semantics.
  Namespace results and arbitrary descriptor return values are never cached.
- NativeFunction has a function pointer, structured signature, traced defaults
  and traced captures, not a hidden Python-reference-bearing C++ closure.
  BoundMethod traces callable/self; NativeMethodDescriptor traces function/owner
  and checks receiver compatibility. Explicit `__call__` wrappers invoke the same
  binder and entry implementation as implicit calling.
- The binder handles positional-only, positional-or-keyword, keyword-only,
  defaults, missing/duplicate/invalid keywords, excess positional values,
  `*args` and `**kwargs`. Tuple/dict materialization is restricted to signatures
  that request variadics. Positional-only names are allowed in captured kwargs
  when a separate positional value supplies that parameter.
- Native entries receive an explicit Attachment, their own rooted function, a
  stable normalized argument span and the caller ReturnSlot. The public call
  boundary first roots inputs, including inputs held only by a reused output,
  before clearing that output. Binder storage stays alive/rooted across reentry.
  A future VM may borrow validated frame spans to remove redundant root work,
  but must reuse these semantics rather than add a second binder.
- Argument/attribute errors use real exception objects with tuple args, Unicode
  messages and traced cause/context/traceback fields. Propagation is exclusively
  Raised, not C++ exception control flow. Chaining/handled-exception/traceback
  execution remains M4 work. Error message wording has not passed the Python
  3.12 differential gate yet.
- bad_alloc at the operation boundary uses a rooted, preallocated MemoryError
  with empty tuple args. The reserve currently has no mutable Python-visible
  traceback/context API. Full low-memory traceback/reentrancy handling must be
  closed with the VM. Other host exceptions remain host faults; a native entry
  that writes no result or returns an invalid Raised payload is an invariant
  error, not a fabricated Python exception.
- Per-attachment call depth bounds native-to-runtime reentry and produces a real
  RecursionError. The limit is configured by `HeapConfig::max_call_depth`.
- Class mutation increments the interpreter type epoch; lookup currently scans
  the same authoritative dictionaries without caches. Builtin mutation is
  rejected. Header/type/native-layout identities remain separate.
- Layout-defined external capacities are accounted at allocation. Name-index
  buffer accounting is maintained incrementally rather than rescanning every
  entry on insertion. Totals still exclude allocator-specific node overhead/RSS.

`Tests/cpp/core/test_call.cpp` exercises the production path under forced GC:
bootstrap failure/retry, successful/raised returns, all binding kinds, descriptor
priority, metaclass lookup, receiver checks, mutation/fallback, recursion,
defaults/captures, output aliasing, MemoryError, Unicode names, interpreter
isolation and fixed-live-set collection of complete temporary method graphs.

M2 remains Experimental. The later construction checkpoint supplies initial
`type.__call__`/`__new__`/`__init__`, class-creation callbacks and a Python error
adapter, but full protocol readiness, layout and remaining type reflection
are not yet supplied.
There is no new Python frontend, CLI or Supported language
subset, and the legacy suite does not establish those new-core capabilities.

## M2 Initial Introspection Descriptors

- Native get/set descriptors carry a traced owner and plain getter/setter entry
  pointers. Their type provides actual `__get__`, `__set__`, `__delete__`
  attributes; binding, owner checks, readonly errors and mutation run through
  the existing descriptor/call machinery. Attribute lookup contains no field-name
  cases for `__class__`, `__dict__`, `__func__` or `__self__`.
- `object.__class__` reads the header type. Assignment requires mutable types,
  equal immutable layouts, equal dictionary capability and compatible solid
  bases. Compatible generic heap classes preserve identity and their existing
  dictionary. Deletion, non-type values and incompatible/immutable transitions
  raise TypeError. The later layout checkpoint extends this to named slots and
  native tails; weakref compatibility is still pending.
- Plain `object` instances do not expose or acquire a dictionary. Generic heap
  types explicitly enable dictionary storage and install an inherited `__dict__`
  descriptor. It returns the live managed dictionary, permits replacement with
  a real dict and clears the instance edge on deletion. Retained old dictionaries
  stay valid; no duplicate field authority is created.
- `type.__dict__` returns a readonly MappingProxyObject with a traced edge to
  the actual namespace. Later class mutation is visible through an existing
  proxy. This slice supplies identity, lifetime and readonly attribute semantics;
  its public mapping operations, views, equality, hash and repr remain M3 work.
  It is not a list snapshot or a second namespace.
- Bound method `__func__` and `__self__` are readonly data descriptors exposing
  the same traced edges used by invocation.
- Object and type default get/set/delete wrappers are installed in their real
  namespaces and use the same implementation as implicit operations. Hooks can
  delegate by calling them. Explicit object lookup on a class sees its own
  namespace, while type lookup also follows class MRO. Object mutation wrappers
  cannot bypass type mutation or builtin immutability.
- Native descriptor binding preserves `optional<Value>` until the explicit
  Python `__get__` boundary: a missing receiver is not Python None. Thus None's
  `__class__` and bound default methods behave correctly. Exact native-type
  guards select the same implementation that their explicit wrappers use;
  arbitrary descriptors still execute their Python-level protocol.
- A native getter roots its descriptor and receiver across reentry. A focused
  regression first reproduced collection of a descriptor whose getter removes
  itself, then passed after adding the temporary root. No GC-skip policy or
  second lifetime mechanism was introduced.
- Native layouts cannot silently discard a generic mixin's dictionary. This
  initial checkpoint rejected that combination; the later tail-field planner
  now represents it. Structural C3 tests also retain a field-free native mixin
  to distinguish instance fields from solid-base compatibility.

`Tests/cpp/core/test_attributes.cpp` exercises these production operations under
collection before every allocation, including compatible/incompatible class
changes, descriptor owner checks, retained dictionaries/proxies, explicit wrapper
delegation, None receiver binding and self-removing getters. No Python 3.12.14
differential or source-language support is inferred from these C++ tests.

## M2 Initial Type Construction

- `Objects/core/construction.cpp` implements object/type construction on the
  existing type-lookup, descriptor-binding, special-call and public call
  operations. Those primitives are shared internal interfaces, not duplicated
  dispatch engines. There is no constructor bypass in typed allocation.
- `type.__call__` invokes `__new__`, skips initialization for an unrelated
  return, and otherwise invokes the actual returned type's `__init__`. It
  preserves the created object across reentry, propagates Raised, and rejects
  an initializer result other than None. The one-argument type query is limited
  to exact type. Metaclass `__call__` overrides use normal special dispatch.
- Object default allocation/initialization enforce their complementary excess
  argument rules. Native storage cannot be allocated as an InstanceObject just
  because its type inherits object; unregistered native constructors currently
  raise TypeError rather than silently manufacture the wrong payload.
- The three-argument type adapter validates managed argument kinds, selects
  the winning metaclass using the same helper as structural TypeReady, invokes
  that winner's overridden constructor before default TypeReady checks, and
  copies the input namespace into a fresh managed dictionary. It translates
  only TypeDefinitionError into Python TypeError; NUL type names use ValueError.
  User callbacks stay outside that definition-error catch.
- StaticMethodObject and ClassMethodObject trace their wrapped callable.
  Static access returns it unchanged; class access supplies the accessed owner,
  including subclasses. Classmethod supports arbitrary callables and the 3.12
  descriptor delegation behavior. Bound-method invocation can prepend a receiver
  to a generic callable without changing the public binder/error path.
- Constructor definitions wrap native `__new__` functions as static methods,
  and `__init_subclass__`/`__class_getitem__` functions as class methods. Future
  PythonFunction support must use the same class-definition preparation step.
- Class creation snapshots and roots original namespace key/value pairs before
  calling `__set_name__`, then invokes the inherited `__init_subclass__` with
  class keywords. Callbacks may erase/replace the namespace and collect. A naming
  error becomes a chained RuntimeError; subclass-hook errors propagate directly.
  User callback side effects are not transactionally undone on failure.
- Type names use one authoritative bootstrap-text-or-managed-str variant.
  During B3 the core image replaces native bootstrap text with real traced
  strings. Reflection returns those same string objects; name/qualname mutation
  stores the assigned string rather than reconstructing an equal object. Module
  is held only in the type namespace, not a second C++ string field. The native
  TypeSpec's default module is an explicit host definition input; the Python
  adapter does not invent a caller module before VM globals exist.
- B3 failure rollback now restores seed names/qualnames and external-byte
  accounting as well as namespace edges. The existing every-managed-allocation
  failure test exposed the initially incomplete rollback and now verifies retry.

The ordering and error-channel cases were checked against the fixed
[CPython 3.12.14 type implementation](https://github.com/python/cpython/blob/v3.12.14/Objects/typeobject.c),
not copied into the new core or linked as a dependency. This is a source-level
semantic reference, **not** a claim of executing the pinned differential suite.

`Tests/cpp/core/test_construction.cpp` adds 24 forced-GC cases: default and custom
construction, returned subtypes/unrelated values, initializer failure/return
validation, metaclass dispatch/selection/conflicts, generic classmethods,
namespace/name identity, callback order/mutation/chaining, and allocation-quota
failure at each default class-construction step with fixed live roots/heap.

Remaining gates include VM closure execution, weakrefs and full slot
iterable protocols, full type reflection and dynamic base mutation,
protocol readiness, UnicodeEncodeError behavior for surrogate type names,
function/decorator metadata, native builtin constructors, and caller-global
module defaults with VM execution. The later cell/readiness checkpoint promotes
preexisting B2 definitions and implements class-cell handling. No new Python
language capability is declared Supported.

## M2 Native Prefix And Named-Slot Tails

- LayoutDescriptor separates native prefix size/alignment/trace/destroy from an
  immutable InstanceFieldPlan (named-slot count and dictionary capability).
  Each allocation stores that full description independently of Type lifetime.
  `storage_size()` includes alignment padding and all field cells, with checked
  size arithmetic. Native access checks the prefix, not equality of whole
  descendant layouts and not Python inheritance alone.
- InstanceObject is now a header-only native prefix. The old inline dictionary
  and duplicate Type `has_instance_dict` flag were removed. Generic, slotted and
  native-prefix instances use the same optional-Value tail cells. Type objects
  retain their intrinsic type namespace, rather than an unused extra dictionary.
- Normal alignment uses matching ordinary new/delete; over-aligned payloads use
  matching aligned new/delete. Payload and field-array lifetimes begin with
  placement construction; failure before adoption destroys initialized storage
  without publishing a heap object. Physical sweep destroys host resources and
  ends tail storage without reading Type or following children.
- GC traces all initialized tail references, including slots whose descriptors
  were removed. Byte pressure and reclamation include the whole prefix/tail
  allocation plus reported external buffers; allocator overhead/RSS remain
  separate measurements, not hidden in the eight-byte header claim.
- TypeReady computes native/solid storage ancestry, selected base, inherited
  field names, optional dictionary and sorted/mangled new member names before
  publication. Independent solid bases conflict even if C++ prefixes or field
  counts match. Dictionary capability survives mixin/native inheritance.
- `__slots__` currently accepts managed str, tuple and name-dict keys. Empty
  slots suppress a new dictionary; an unslotted subclass adds one; `__dict__`
  explicitly requests one and rejects duplicates/inherited duplicates. Native
  flags reject nonempty slots for tuple/type-derived layouts. Invalid names
  raise TypeError and class-variable conflicts raise ValueError.
- MemberDescriptorObject traces its owner and stores a stable field index. It
  uses the existing binding/call operations and data-descriptor priority, checks
  receiver semantics, preserves unbound vs real None, and raises AttributeError
  for missing/deleted fields. Explicit wrappers and implicit access share code;
  no attribute-name branches or parallel instance-field map were introduced.
  Identity is boxed identity; common equality/hash/repr protocols remain part
  of the incomplete general ObjectOps gate, not per-descriptor shortcuts.
- Inherited dictionary descriptors locate storage from the actual allocation,
  not an ancestor's offset. Adding descendant slots therefore cannot alias or
  overwrite the inherited dictionary. Changing the Python `__slots__` attribute
  later does not rewrite the approved layout or existing field cells.
- Class reassignment checks complete layout and field names, then peels only
  field-free/dictionary-only inheritance steps and compares compatible storage
  ancestry. Sibling layouts can match, but equal final names do not make two
  unrelated deep slot hierarchies or native storage roots interchangeable.
- Identifier checking uses generated Unicode 15.0.0 XID_Start/XID_Continue
  ranges plus underscore, not locale or the host's Unicode 14 ICU/Python tables.
  `Tools/generate_unicode_identifiers.py` regenerates the checked-in data from
  the official versioned UCD source. The generated header records its SHA-256;
  `doc/licenses/unicode.txt` preserves the official Unicode license. Builds need
  neither ICU nor an online fetch. Slot names are not NFKC-normalized here;
  source identifier normalization belongs to the future frontend.

`Tests/cpp/core/test_layout.cpp` adds 19 production-path forced-GC tests covering
header-only empty layouts, unbound/None, descriptor ownership/shadowing, inherited
and repeated members, dictionary offset changes, mixed native inheritance,
over-alignment, construction failure, same-batch Type/instance sweep, cycles,
class reassignment, frozen layouts, allocation quotas and NativeWait/another
attachment mutating a tail then collecting. Unicode cases include Kawi added in
15.0, combining marks, surrogates, emoji and private-name mangling.

The existing Release cost probe now also checks slot get/set and prints the
actual instance storage size (40 bytes for an eight-byte generic prefix and two
16-byte optional-Value cells). These cells are an explicit initial representation
cost, not a claim of CPython memory parity. Raw intermediate and final samples
are retained in `benchmarks/foundation-layout-2026-10-29.csv`.

References: [Python 3.12 slots contract](https://docs.python.org/3.12/reference/datamodel.html#slots),
[Unicode 15.0.0 property data](https://www.unicode.org/Public/15.0.0/ucd/DerivedCoreProperties.txt).
These references and C++ tests do not replace pinned source-language differential
execution. Arbitrary iterable slot sources, weakref/finalizer behavior,
complete type reflection/mutation and protocol readiness remain gates.

## M2 Cells And B2 Runtime Readiness

- CellObject is a boxed, traced object holding one optional Value. Empty is not
  Python None. Its native constructor accepts zero/one positional arguments,
  rejects keywords and subclassing, and uses the ordinary type-call path.
  `cell_contents` is an actual get/set descriptor: empty reads raise ValueError,
  assignment retains the given identity, and deletion (including repeated
  deletion) clears the edge. Native callable captures can share the same cell;
  GC traces the current contents and collects unrooted cycles without RC.
- Type construction validates `__classcell__` by Python kind, not C++ layout,
  prepares native static/class method definitions before cell publication, then
  writes the class into the existing cell and removes the key from its copied
  namespace. The original input dictionary is not changed. Naming/subclass
  callbacks see the filled cell and may change it; their effects are not undone
  when a later callback fails.
- Python 3.12's namespace-cell hook is also represented: `__classdictcell__`
  receives the actual copied DictObject, not a snapshot/proxy/input dictionary,
  and is consumed before callbacks. Aliasing both hooks uses definition write
  order; an invalid namespace cell does not undo an earlier valid class-cell
  publication. This does not implement annotation scopes or build_class yet.
- TypeState explicitly distinguishes Building, Structural and Ready. Ready
  means native names, namespace and descriptors are complete for the currently
  implemented capabilities; it is not a claim that all M2 protocols are done.
  Normal creation and bootstrap promotion share one `detail::type_ready` path.
  A native return cannot expose a Building type; that invariant fault clears the
  caller result slot and stays a host fault rather than a fabricated Python error.
- Pre-B3 definitions retain only temporary native module metadata until it can
  become a managed namespace entry, then discard that native authority. B2-only
  managed namespace input is explicitly rejected rather than silently ignored;
  invalid native codepoints are rejected before poisoning later bootstrap.
- bootstrap_core snapshots all existing structural Python types, not just the
  two seed names. Under the execution lock and bounded no-callback/no-GC boot
  transaction, it stages the native image and promotes existing types in place.
  Existing type/metaclass identity, instance headers, slots, module and qualname
  survive. The image becomes published only after all promotions complete.
- Failure restores every prior type's name/qualname/module/dictionary/state and
  byte accounting, clears the staged interpreter image, restores GC policy,
  and collects abandoned storage. The same attachment can retry. Temporary
  image availability for native factories is separate from public core_ready;
  it is not a second runtime or a global active-context fallback.

`Tests/cpp/core/test_cell.cpp` adds 12 forced-GC cases, including shared captures,
cycles, consumed namespace hooks, callback mutation/failure, aliased cells and
allocation failure without unprepared method publication. `test_readiness.cpp`
adds six cases for existing B2 instances/metaclasses, every managed bootstrap
and promotion allocation failing/retrying, idempotence, early namespace/text
rejection and an invalid native return.

Reference semantics were checked against the fixed
[CPython 3.12.14 cell implementation](https://github.com/python/cpython/blob/v3.12.14/Objects/cellobject.c)
and [type implementation](https://github.com/python/cpython/blob/v3.12.14/Objects/typeobject.c).
This is source-reference work, not execution of a pinned differential suite.
At this checkpoint cell content comparison, unhashability and repr awaited
common ObjectOps; the hash/representation checkpoint below now supplies them.
VM dereference errors, closure/function metadata and class-body cell validation
remain M4-M5 work. Custom metaclass MRO hooks and exact failed-definition cell
visibility are also unresolved acceptance cases: this staged implementation
does not publish a class cell before structural C3/layout checks succeed.
No complete cell/type/source-language compatibility claim follows from Ready.

## M2 Common Protocols And M3 Integer Seed

The Experimental integer implementation uses a signed int64 payload or an owned host-only
Boost.Multiprecision cpp_int, with no Python references hidden in that backend.
Boost headers (minimum 1.74) are a private build dependency; no Boost shared
library or network fetch is part of normal configuration. Configure
BOOST_MULTIPRECISION_INCLUDE_DIR when headers are outside the normal paths.
GMP is not used: its documented process-wide allocation/fatal-OOM behavior does
not provide the existing per-interpreter MemoryError recovery contract.

Public operation results use caller ReturnSlots. Protocol resolution records
the lookup type, actual MRO owner/descriptor and interpreter type epoch, with
rooted handles. It distinguishes missing/disabled from NotImplemented, and
re-resolves stale snapshots or a changed receiver type. No per-type cache or
alternate call engine was added: implicit calls reuse the descriptor/binder
path, and the dictionaries expose the same numeric native entries.

- Truth requires an actual bool result from __bool__, otherwise uses checked
  __len__ or defaults true. Length accepts __index__, rejects negative values
  before address-width overflow checking, and returns an exact int. Index
  canonicalizes int subclasses without consulting an overridden __index__.
- Rich comparison preserves arbitrary results, gives the strict right subtype
  priority, and tries the reverse comparison even for equal operand types.
  Only final eq/ne fallback uses identity; container_equal separately applies
  its identity shortcut before equality and truth conversion.
- Five binary operations (+, -, *, //, %) have separate normal/reflected rules.
  Right-subtype overload detection performs right/left class attribute lookup,
  descriptor binding, identity-aware inequality and truth, not thunk-pointer
  comparison. Each later attempt resolves again after callbacks. Inplace first
  tries its own method and falls back only for missing/NotImplemented.
- Native integer operations keep int64 arithmetic while it fits, promote on
  overflow, and normalize results back. Floor division/modulo use Python's
  divisor-sign rules, including INT64_MIN / -1 without C++ signed overflow.
  Division by zero raises a rooted ZeroDivisionError. Native layout alone does
  not confer int semantics; bool is an int subtype with distinct singletons.
- Exact int conversion retains identity; int subclasses keep their own native
  prefix, dictionary and identity. int(x) honors a subclass __int__ override,
  whereas index(x) reads the integer value. int.__new__(bool, ...) is rejected.
  The native decimal factory is only a C++ literal/data entry, not Python int's
  full string/base parser. Current exact-int caching covers zero and one only.
- TypeReady inserts __hash__ = None only for an own __eq__ with no own hash;
  later attribute mutations do not rerun this class-definition rule. Hash
  resolution distinguishes an explicitly disabled method. The following
  checkpoint adds general hashing and integer hash values.

`Tests/cpp/core/test_protocol.cpp` adds 15 cases and `test_number.cpp` adds nine,
with collect-every-allocation fixtures. They cover callback mutation, custom
descriptor overload comparison, same-type reverse rules, full C3, epoch
invalidation, instance-hook bypass, recursive entry limits, foreign interpreter
rejection, arbitrary precision, allocation quota recovery and conversion/layout
boundaries. Existing every-allocation bootstrap/promotion failure tests also
exercise the larger native seed image. Final suite: 163/163 foundation,
411/411 full CTest, and 163/163 each ASan/UBSan/LSan and TSan.

The cost probe measures the same public binary entry and validates values and
the post-collection live set. Small IntObject storage is 24 bytes excluding heap
metadata/external buffers. Raw numeric samples are in
`benchmarks/foundation-numeric-2026-10-29.csv`; unchanged probes are appended to
the layout CSV. These are initial generic costs, not an optimization/parity claim.

The protocol and builtin matrices remain Experimental. Full integer text/base
parsing and operators, float/mixed numeric behavior, warning machinery (including
Python 3.12 NotImplemented truth and strict-subclass conversion warnings), and
complete container/iteration semantics remain gates. NotImplemented is an actual rooted
singleton; its current generic truth is true without a DeprecationWarning.
No VM, frontend or source-level performance acceptance follows from these tests.

Reference: fixed [CPython 3.12.14 type slots](https://github.com/python/cpython/blob/v3.12.14/Objects/typeobject.c)
and [abstract operations](https://github.com/python/cpython/blob/v3.12.14/Objects/abstract.c).
Backend contract: [Boost 1.74 cpp_int](https://www.boost.org/doc/libs/1_74_0/libs/multiprecision/doc/html/boost_multiprecision/tut/ints/cpp_int.html)
and [GMP allocation limitations](https://gmplib.org/manual/Custom-Allocation).
Source checks do not replace a pinned executable differential suite.

## M2 Hash And Representation / M3 Value Semantics

The common hash/repr/str entries use the same type/MRO resolution, descriptor
binding, native binder, call-depth accounting and caller result roots as the
other ObjectOps. They ignore instance shadowing, propagate Raised results, and
check Python kinds separately from physical layouts. String results may be real
str subclasses; integer hash results may be int subclasses/bools, but objects
that only supply __index__ are not accepted as hash results. Huge user hash
integers use the numeric hash reduction; -1 is normalized to -2.

- object supplies stable identity hashing, metadata-based repr and str-to-repr
  dispatch. Type repr uses actual module/name/qualname metadata. None and
  NotImplemented have literal repr; bool and int retain their numeric wrappers.
- Integer hashing uses a signed-width Mersenne modulus, with the same mapping
  for bool and equal ints. It handles INT64_MIN and arbitrary-precision limbs
  without narrowing first. Tuple hashing delegates every element to common
  hash and combines the resulting words; failures propagate without caching.
- Each interpreter owns an immutable SipHash-2-4 key initialized at host-state
  construction with std::random_device. There is no process-global hash key.
  UTF-32 str payloads currently have one canonical byte representation; equal
  values therefore hash equally regardless of object identity/subclass. Empty
  str hashes to zero. Entropy failure is a pre-Python host initialization error.
  PYTHONHASHSEED/InitConfig selection and compact Unicode storage remain later
  platform/object work, not hidden environment reads in this slice.
- Str has codepoint equality/ordering, value hashing, native str conversion and
  repr quoting/escaping. Repr printable categories come from fixed Unicode 15
  DerivedGeneralCategory data, not locale/ICU. Surrogates remain valid stored
  codepoints and are escaped only by repr; raw str conversion does not encode.
  The existing generator now also emits unicode_nonprintable.inc with source
  SHA/version/license. Its original XID output is unchanged and reproduced.
- Tuple comparison uses container_equal for prefixes and preserves arbitrary
  rich-ordering results at the first unequal element. Repr uses element repr,
  singleton commas and a scoped identity guard. Guard state belongs to the
  active Attachment (including across NativeWait), not the interpreter/global
  namespace. It borrows identities whose callers keep rooted and unwinds on
  both Python and C++ error paths. User repr recursion is a RecursionError,
  not the container's ellipsis behavior.
- Cells compare current contents through rich_compare (empty sorts first),
  remain unhashable and describe content type/address without calling its repr.
  Mutable B3 name dictionaries are unhashable; mappingproxy hashing delegates
  to its actual mapping. BaseException repr/str use the argument protocols.

`test_representation.cpp` adds 17 tests: reference SipHash boundary vectors;
numeric/hash consistency; disabled/invalid/huge custom hashes; Unicode repr;
str subclasses and fake native layouts; type/singleton/exception repr; tuple
equality/hash/ordering/recursion; cell semantics; OOM and explicit wrapper
equivalence; recursive user callbacks; and a second attachment rendering a
container whose identity is guarded by the first attachment in NativeWait.
Final tests: 180/180 foundation, 428/428 full CTest, 180/180 ASan/UBSan/LSan and
180/180 TSan. Both generated Unicode tables reproduced byte-for-byte.

Existing cost probes are retained, with additional public string-hash and tuple
repr timing plus result/final-live-set checks. Raw rows are marked
hash_representation_final in the layout/numeric CSVs. Metadata/RSS and Python
source workloads still need their own acceptance evidence; these measurements
do not demonstrate CPython parity. General mutable containers, iterators,
remaining callable/descriptor value semantics, warning machinery, complete
type mutation and weakref/finalization remain open. Full Unicode codecs,
constructors, compact storage and exact long/non-ASCII cell type-name formatting
also remain Experimental gates, rather than implied by the new repr entry.

References: [CPython 3.12.14 generic object operations](https://github.com/python/cpython/blob/v3.12.14/Objects/object.c),
[tuple semantics](https://github.com/python/cpython/blob/v3.12.14/Objects/tupleobject.c),
[cell semantics](https://github.com/python/cpython/blob/v3.12.14/Objects/cellobject.c),
[type/object metadata repr](https://github.com/python/cpython/blob/v3.12.14/Objects/typeobject.c),
[Unicode 15 categories](https://www.unicode.org/Public/15.0.0/ucd/extracted/DerivedGeneralCategory.txt)
and [SipHash reference vectors](https://github.com/veorq/SipHash/blob/master/vectors.h).
Hash numbers for randomized strings/addresses are not compatibility constants.

## M4 First Verified Returning Executor

CodeBuilder owns a normal RootScope for constants while constructing its private
instruction stream. Labels are local to the builder; freeze resolves labels,
validates the entire instruction/operand/source stream, then checks reachable
stack heights. Each PC is queued at most once, and a mismatched merge (including
a positive-growth back edge) is rejected. Zero-growth infinite loops are legal
to verify; verification is bounded, not a program termination proof.

The initial in-memory encoding is `Instruction{Opcode:uint8, operand:uint32}`.
It is an ordinary C++ struct, not packed bytes or a persistence/CPython co_code
format. Instruction indices are PCs. One OpcodeDefinition table supplies the
name, required inputs, pop/push counts, operand domain, normal successors,
exception capability and safepoint status used by validation/disassembly.
Exceptions currently leave the returning executor through Raised; no local
handler/cleanup/resume opcodes or alternate exceptional successors are claimed.

| Opcode | Stack effect | Operand / behavior |
| --- | --- | --- |
| LOAD_CONST | +1 | Constant tuple index |
| LOAD_FAST | +1 | Local index; unbound raises UnboundLocalError |
| STORE_FAST | -1 | Local index |
| DELETE_FAST | 0 | Local index; deleting unbound raises UnboundLocalError |
| POP_TOP | -1 | No operand |
| DUP_TOP | +1 | Requires one input, no operand |
| BINARY | -1 | Existing BinaryOp domain; same reflected/NotImplemented rules |
| COMPARE | -1 | Existing CompareOp domain; keeps arbitrary result |
| JUMP | 0 | Absolute instruction-index target |
| JUMP_IF_FALSE | -1 | Target; common truth protocol and its errors |
| RETURN | -1 | Transfers top value to caller before callee roots unwind |
| RAISE_INSTANCE | -1 | Requires an already normalized BaseException instance |

Unknown opcodes, reserved operands, bounds and malformed source positions are
checked even in unreachable code. Reachable underflow, fallthrough and unequal
merges are rejected with typed CodeValidationError (kind/PC). Source positions
are either wholly unknown zeros or positive ordered spans; first_line is
positive. Region, handler and resume validation is still a later acceptance gate,
not inferred from rejection of opcodes outside this initial instruction set.

Only CodeBuilder can construct the move-only VerifiedCode proof. CodeObject
stores it as const, with actual managed co_consts/co_varnames/name/qualname/file
objects. A Function holds the original Code Value, not a reconstruction or a
second code representation; multiple functions may share that identity. Empty
and absent function defaults remain distinct. Code signature shape is immutable;
positional/keyword defaults belong to Function. A shared bind_arguments
implementation now serves both native and bytecode functions, including all
existing parameter kinds, variadics and default rules.

Objects/call depends on a typed per-interpreter CallExecutor registered only
after successful bootstrap publication, not on VM headers or a global VM. Failed
bootstrap leaves no executor installed. Execution layouts and metadata wrappers
come from the execution subsystem. Function instance dictionaries use the same
TypeReady field planner and descriptor machinery as other native prefixes.

Frame is a GC object. Its function and chosen Code are traced, locals/operands
are fixed optional-Value arrays, and operand capacity comes from max_stack.
Operands remain in rooted Frame cells throughout allocating/callback operations.
Active execution roots the Frame normally; Attachment.current_frame borrows that
rooted identity for native callbacks. Native reentry creates another Frame with
an owned back edge and restores the previous active Frame on exit. Normal,
Raised and C++ host-fault exits clear operand cells and mark Completed; captured
Frames retain locals/code for observation. Physical destruction remains callback
free. The current Frame states are Created/Running/Completed; no fake Suspended
state or yield path was added before its continuation model exists.

Code/function/frame have distinct semantic types and physical layouts, traced
edges, metadata descriptors and bounded repr entries. Code identity and equality/
hash are currently the internal object's identity; CPython structural code
equality/hash, CodeType construction and complete reflection are still explicit
Experimental gates. Function/frame identity equality/hash follow their ordinary
object contract. Function metadata setters, closures/cells, globals execution,
module/class mapping frames and full frame locals observation remain work.

The executor uses switch dispatch and calls existing ObjectOps for arithmetic,
comparison and truth. It currently exposes only ordinary returning calls through
call(), not an incomplete execute/resume payload ABI. CALL is deliberately not
in this opcode set: ordinary Python-to-Python scheduling must be added without
recursive C++ execute calls. Native-to-Python reentry already uses the existing
call-depth limit. Raise class/bare/cause normalization, traceback construction,
regions/unwind/finally/with and typed Yield/resume are not implemented here.

The execution tests cover malformed streams, bounded back-edge validation,
disassembly, Code identity, shared binding/descriptors/special methods, defaults
and variadic transfer, loops/locals, GC and source-position observation during
callbacks, native reentry/back edges, host-fault cleanup, allocation failure at
every freeze allocation and collection of function/globals/code cycles. This
is a native CodeBuilder vertical slice, not a new source frontend or full M4.

## M4 Non-Recursive Calls And Rooted Continuations

CALL uses a private call-site index as its operand. Immutable CallLayout entries
hold positional/keyword counts; the actual keyword names are managed str tuples
owned and traced by Code. CodeBuilder.call creates those entries. The central
instruction_effect derives CALL's required inputs/pops from that layout: callee,
positional values, then keyword values are consumed and one result is produced.
Validation rejects bad call-site indices, oversized arity and reachable input
underflow; disassembly uses the same opcode metadata. The instruction encoding
remains the existing private in-memory Opcode/uint32 pair.

The call engine gained an explicit prepare_call mode, not a second resolver.
Native callables complete normally into the caller ReturnSlot. A final Python
target is instead returned as PreparedPythonCall with owned argument snapshots
and handles rooted in the caller's supplied RootScope. An actual None receiver
is distinct from no receiver. The target is not a Python Value or an artificial
Frame return. Bound methods, generic callable __call__, descriptor binding,
receiver prepending and binder errors keep the ordinary call path. Descriptor
callbacks may use bounded native-to-Python reentry during preparation.

The VM creates a child Frame with the existing binder/factory, parks the caller
with its argument stack intact, and changes the active rooted Frame. One active
ReturnSlot roots the top; owned back edges keep parked parents alive. pending_call
records the caller's stack destination while pc already names the continuation.
Child return validates readiness through the same validator as ordinary calls,
publishes into the parent's traced operand slot, then releases the child root.
No Python-to-Python call recursively invokes C++ execute. Native reentry opens
its own explicit execution boundary and restores its previous active Frame.

Frame locals remain optional cells for unbound-versus-None semantics. Operands
are now a preallocated contiguous Value array: sp alone defines its active
prefix, only that prefix is traced, and released slots are overwritten with the
interpreter's rooted None padding. None is not an unbound or call-state marker.
Positional CallArgsView can borrow this stable array across native reentry;
Python targets snapshot before temporary normalization scopes disappear.

HeapConfig separates max_call_depth (logical frames/dispatch budget) from
max_native_depth (active C++ call-dispatch budget). Both default to 128 to retain
the prior internal bound. Scheduled children retain logical permits only;
ordinary/native dispatch guards retain both. Completion, Raised and C++ host
faults iteratively clear every active child and pending call, balance permits
and restore the boundary's predecessor. This is an internal runtime budget, not
an implemented Python sys recursion-limit API or platform stack-size detector.

The new scheduling suite adds 11 cases: call metadata/effects; Python/native
keyword binding; bound and __call__ methods; failed/missing calls; prepared
snapshots surviving loss of the original bound-method roots; parked Frame GC;
allocation failure/retry; exception and host-fault cleanup; return-readiness
equivalence; and independent logical/native recursion limits. With native depth
16, 6000 ordinary Python calls succeed and return to the original live set.
A regression initially demonstrated that a scheduled unready return could be
hidden by the caller; sharing validate_return removed that semantic difference.

Verification: 208/208 foundation, 456/456 full CTest, 208/208 ASan/UBSan/LSan and
208/208 TSan. Direct returning and scheduled-call cost probes both validate the
result and post-GC live set, with all raw samples retained. Frame prefix is now
144 bytes including continuation metadata; each operand cell is eight bytes,
while each optional local remains 16. These exclude buffers/heap/root metadata
and are not CPython performance or memory acceptance results.

The scope remains M0-M9. Region/handler/cleanup validation, traceback, full
exception normalization/context, finally/with, closure and namespace execution,
typed Yield/resume, source compilation and release/performance gates remain
unfinished. CALL scheduling does not approximate or close those requirements.

## M4 Tracebacks On Explicit Raised Paths

Traceback is a managed, non-subclassable object with traced next/actual-Frame
edges, immutable frame/lasti/lineno fields and no external buffer. It uses the
ordinary identity equality/hash and object repr/str protocols. No synthetic
Frame or duplicate Code is manufactured for reflection. Completed Frames retain
their real function, Code, locals and back edge, but release operand/pending-call
state. The traceback object is 48 bytes on the current x86-64 build, before heap
allocation metadata. Frame size remains 144 bytes; successful calls do not
allocate tracebacks.

At an explicit Raised exit, FrameActivation iteratively prepends the current
Frame before releasing its active root and advancing to its parent. This also
balances logical call permits. The boundary root is completed by the existing
activation lifetime. Native-to-Python reentry constructs its own traceback
segment; propagation through the native caller then adds that caller's Python
Frame once. Native functions themselves do not acquire fake Python Frames.
Binding failures before a callee Frame exists identify the caller's CALL.
Repeated explicit RAISE_INSTANCE prepends a new node and preserves the old chain.
No C++ host fault is relabeled as a Python exception or traceback.

tb_lasti is an instruction index in our private in-memory bytecode, not a
CPython byte offset. tb_lineno snapshots the failing instruction's source line,
using co_firstlineno when that position is unknown. Manually constructed nodes
with lineno=-1 resolve against their stored lasti, not the Frame's later PC;
an out-of-range index returns None. Constructor positions use index conversion
and signed 32-bit range checks, following the Python 3.12 constructor's C-int
contract. Source position storage itself remains the existing uint32 format.

tb_next accepts None or another traceback, checks for a loop before mutation
and rejects deletion. tb_frame, tb_lasti and tb_lineno are read-only. The
BaseException.__traceback__ descriptor and with_traceback method validate the
same None/traceback contract; the latter returns the original exception.
All construction, calls, binding and descriptors reuse existing ObjectOps.
Exception/traceback/Frame/local cycles are ordinary tracing-GC graphs and are
collected after their last external root disappears.

If traceback allocation fails, the boundary returns the preallocated emergency
MemoryError. Automatic propagation never attaches a traceback to that shared
reserve: doing so would permanently retain arbitrary failed execution graphs.
A previously raised exception may retain a partially extended traceback.
Full Python low-memory chaining/reserve reentrancy is still an Experimental
gate, not claimed to match CPython's emergency allocator behavior. This does
not exclude ordinary, separately allocated MemoryError instances from normal
traceback propagation. User assignment of __traceback__ remains explicit.

Tests cover forced collection, scheduled and native-reentrant ordering, source
positions, binding errors, repeated raises, mutation failures, construction and
GC during index conversion, identity protocols, OOM/retry and cycle collection.
A separate test propagates 6001 traceback nodes with native depth limited to
16, validates completed Frames and the original live set, then retries.

Python 3.12 reference contracts: [traceback objects](https://docs.python.org/3.12/reference/datamodel.html#traceback-objects),
[BaseException.with_traceback](https://docs.python.org/3.12/library/exceptions.html#BaseException.with_traceback),
and the fixed [CPython v3.12.14 traceback implementation](https://github.com/python/cpython/blob/v3.12.14/Python/traceback.c).
These contract checks are not a pinned executable source-language differential
suite. Region/handler/cleanup validation, bare raise, raise class/cause and
handled-exception context, finally/with, closure/namespace execution and typed
suspension/resume remain M4 requirements. M0-M9 remains active.

## M4 Structured Regions And Traced Unwind

CodeBuilder declares Loop, Except and Finally regions, enters/leaves their
protected bodies and marks handler boundaries. Each region has one immutable
parent token (including Protected/Handler/Cleanup phase), entry/exit/handler
PCs and a lexical depth. Loops additionally require explicit continue and break
labels. Region IDs and labels belong to their builder. Ordinary branches keep
their region token; crossing a boundary, entering a handler by a raw jump,
incomplete/misnested regions and inactive loop targets are rejected before Code
publication, including invalid operands in unreachable instructions.

| Opcode | Normal Stack Effect | Region/Exceptional Effect |
| --- | --- | --- |
| ENTER_REGION id | 0 | Records the current stack base in a preallocated active-region array |
| LEAVE_REGION id | 0 | Normal(after_pc, parent); skips except handlers or enters finally |
| END_HANDLER id | 0 | Restores the previous handled exception; finally resumes its saved reason |
| BREAK_LOOP id | 0 | Unwinds to and leaves the named active loop |
| CONTINUE_LOOP id | 0 | Unwinds to the named loop, retaining that loop and its outer regions |
| RERAISE | 0 | Raises the handled exception without adding a traceback node; absent state raises RuntimeError |
| LOAD_EXCEPTION | +1 | Pushes the actual handled exception, or None outside handling |
| EXCEPTION_MATCH | -1 | Consumes exception/matcher, produces bool; invalid instances/classes raise TypeError |

All eight opcodes use the existing Opcode/uint32 private encoding and central
metadata/disassembly. Matcher classes must really derive from BaseException;
flat tuples validate every member even when an earlier one matches. Actual
subtype checks do not call a metaclass __subclasscheck__. Raise class/cause
normalization and exception groups are not supplied by RAISE_INSTANCE.

The verifier joins one stack height per PC and propagates exceptional edges
through lexical parents. A protected prefix cannot be consumed; normal region
exits and handler ends must restore their base. Except entry gets base+1 for
the exception; finally entry gets base. Each finally records a finite set of
abstract (reason, destination) continuations. Discovering a new continuation
requeues only that cleanup's end, so forward discovery cannot hide a normal
or loop successor. Growing stack states still fail a merge; the verifier does
not enumerate arbitrary heights or recurse over nested regions.

Frames own typed NormalExit/FunctionReturn/ExceptionRaise/LoopBreak/LoopContinue
reasons, active-region state and one saved continuation for each entered
cleanup. Arrays reserve the verified maximum lexical depth during Frame
construction and their capacity counts toward heap pressure. A reason owns
its payload before stack trimming. Each exited region restores only its own
stack base; outer operand prefixes remain alive. Normal cleanup completion
resumes its saved reason. A new return/raise/break/continue discards only the
continuations it actually exits. An exception caught inside a cleanup leaves
the enclosing cleanup's saved reason intact. All reason payloads and saved
exception contexts are traced fields, not Handles from a temporary RootScope.

Raised exits no longer complete the whole chain before handlers can see them.
The current Frame first gets its traceback node, then unwinds its own regions.
Only an uncaught error crosses to a parked parent, rooted in the parent's
reason before the child is cleared. Failed CALL operands/pending state are
released, and the parent then adds its own CALL traceback node and searches its
regions. Resuming an old Raise from finally or using bare RERAISE does not add
a duplicate node. Native reentry and scheduled Python calls inherit and restore
the same per-Frame handled exception. The Attachment view borrows that rooted
Frame field; it is not a second owner or interpreter-global exception state.

Native raise_error and explicit instance raises link implicit exception
context at the origin, not at each propagation hop. Context linking avoids
creating a new context cycle and terminates on existing user-created cycles.
The emergency MemoryError reserve still receives no automatic mutable context
or traceback. Managed/host bad_alloc during an instruction or a reason transfer
becomes that reserve inside the current Frame, allowing handlers and finally
to execute without allocating unwind storage. Other C++ faults still leave
through the host-fault path, clearing internal state rather than fabricating a
Python exception. Frame completion clears all transient unwind/handled fields.

Fifteen focused tests exercise validation, late cleanup-continuation discovery,
normal except/finally exits,
ancestor catch and actual tracebacks, GC with pending temporary return values,
nested internally caught errors, return/error/break overrides, loop targeting
and operand preservation, Python/native inherited bare raise, allocation failure
catch/retry, matcher rejection and existing/new context cycles. The invalid
EXCEPTION_MATCH instance test initially failed and now raises TypeError.
The older 6000-deep return/error scheduling and lifecycle suites remain enabled.

Reference: fixed [CPython v3.12.14 raise logic](https://github.com/python/cpython/blob/v3.12.14/Python/ceval.c)
and [implicit exception context handling](https://github.com/python/cpython/blob/v3.12.14/Python/errors.c).
There is still no new source frontend or pinned executable language differential
suite. With enter/exit lookup, suppression and binding cleanup; except-as target
cleanup lowering; class/cause normalization and full exception reflection;
yield/send/throw/close; closures and namespace frames remain M4-M5 gates. This
is an Experimental execution slice, not complete M4 or Python-language support.

## M4 With Cleanup Through Ordinary CALL

ProtocolDefinition now includes Enter/Exit with arities zero/three and Any
results. PREPARE_WITH resolves the actual type's __enter__ descriptor, binds it,
then freshly resolves/binds __exit__. Instance attributes and __getattribute__
are bypassed. Enter descriptor side effects may change the manager's type;
exit lookup uses that new type. Both bound callables are rooted before enter is
called, and exit is not re-resolved if enter subsequently changes the class.

CodeBuilder.enter(With) emits PREPARE_WITH, ordinary CALL 0 and ENTER_WITH.
Only the last instruction registers the region, after enter has returned.
The operand prefix holds the cached exit below the enter result. stack_base
protects that exit but allows consuming the result for target binding. Failures
in target binding therefore unwind the registered region. No extra owner,
per-Frame field or context-manager wrapper object was added.

CodeBuilder.leave(With) emits its fixed cleanup template: WITH_EXIT, ordinary
CALL 3, WITH_RESULT, END_HANDLER. The verifier checks template positions and
arity, protects the cached exit and adjusts parent heights when releasing it.
An abstract Raise continuation has both propagation and possible-suppression
successors; this remains within the finite continuation verifier.

| Opcode | Inputs / Pops / Pushes | Behavior |
| --- | --- | --- |
| PREPARE_WITH | 1 / 1 / 2 | Manager to cached exit and bound enter; lookup/binding may raise |
| ENTER_WITH id | 2 / 0 / 0 | Registers with enter result still above protected exit |
| WITH_EXIT id | 0 / 0 / 4 | Pushes cached exit and type/value/traceback, or three None arguments |
| WITH_RESULT id | 1 / 1 / 0 | Truth-converts only for a pending Raise; truthy means Normal after the region |

Both enter and exit calls use the existing non-recursive Python Frame scheduler
and shared native binder. Normal/return/break/continue exits ignore the exit
result without truth conversion. Exceptional exit uses the original exception
and its actual traceback. Exit or truth-conversion errors override the pending
reason and retain implicit context. Suppression restores the previous handled
exception; outer managers then observe a normal exit. The cached exit is
released on both successful completion and an overriding error.

Sixteen new tests cover normal binding, real exception arguments, suppression,
enter/missing-exit/exit failures, nonexceptional truth avoidance, target binding
failure through real ObjectOps, nesting, implicit descriptors and hooks, type
mutation during descriptor binding and enter, loop transfers, inherited handled
state, allocation-free exit after OOM and cached-prefix verifier rejection.
With Python enter/exit methods and native depth capped at 16, 601 nested contexts
complete and return to their original post-GC live set. Frame prefix and region/
continuation representations are unchanged by this slice.

The lookup/call ordering and exception arguments were checked against the fixed
[CPython v3.12.14 with instructions](https://github.com/python/cpython/blob/v3.12.14/Python/bytecodes.c)
and the [Python 3.12 with contract](https://docs.python.org/3.12/reference/compound_stmts.html#the-with-statement).
This is native CodeBuilder verification, not source-language support or the
pinned executable differential gate. Async with, target unpacking/lowering,
exception groups and the remaining M4-M9 gates are not inferred from it.

## Concurrency Route Review Hold (Historical)

The user's latest requirement is real CPU-parallel Python execution in one
interpreter with a shared heap. NativeWait/I/O progress or separate interpreters
are not substitutes. The implemented foundation still holds State::execution
for an Attachment's attached lifetime; this is serial execution, not no-GIL.
The original plan/AGENTS concurrency choice has not been silently rewritten.

At that checkpoint the goal was paused for the user's route review. No new
synchronization design was implemented there. Plan 2 now supersedes that hold
and the conflicting per-interpreter-lock final target. Review topics included mutator
registration and safe points, root publication and GC stop coordination,
heap/root bookkeeping, shared mutable-object synchronization without locks
across Python callbacks, and thread exit/shutdown. Acceptance must demonstrate
overlapping CPU execution in the same interpreter, shared-object correctness,
GC stress and race/deadlock checks, not merely passing the current serial suite.

## Plan 2 Adoption And First Root-Arena Migration (Historical)

The user supplied cxxpython_foundation_redesign_plan_2.md, replaced the old goal
and set its N0-N6/M0-M9 objective active. No additional approval of the shared-
Interpreter/heap CPU-parallel requirement is outstanding. The original plan is
retained as history. AGENTS now points to the second plan and explicitly rejects
an execution GIL, separate interpreters and NativeWait as final CPU evidence.

The first task-02 implementation moves RootCell deque/free-list/count into one
RootArena owned by each registered ThreadRecord. Registration and retirement
currently use the existing transitional admission mutex. The State registry and
Attachment hold native shared leases on records; this is not Python object RC.
The current collector scans every registered arena, including owners frozen in
NativeWait, rather than one global arena or only its requesting thread's roots.
Retirement occurs with no live roots and removes that arena's cells/storage.

Handles contain Interpreter identity, monotonic per-Interpreter arena identity,
cell address and cell generation. The arena check happens before dereferencing
the cell, rejecting foreign/retired-arena handles even on the same OS thread.
Within an arena, existing non-LIFO reuse, stale generations and ResultView
revision validation remain unchanged. Ordinary Handle/ReturnSlot/ResultView
do not become cross-thread transfer objects. Three regressions first failed
against the global-arena implementation, then passed after this migration.

ReturnSlot's trusted internal store writes only an existing caller-owned cell's
object/tag/revision and is noexcept, without allocation, heap lookup or waiting.
Managed allocation uses it after output validation and successful domain
registration. Public ok/raised still validate inputs; they are not yet the
shared-field load-to-root API. RootArena growth remains a safepoint-free host
allocation under the current serialized implementation; bounded NoSafepoint
access and pre-reserved roots still require the next N1/N2 work.

HeapStats now aggregates root counts/cells over registered records and reports
registered_threads. Recorded metadata adds sizeof(ThreadRecord), but excludes
shared_ptr control blocks, deque blocks, registry capacity, allocator overhead
and RSS. Handles grow from 24 to 32 bytes on this x86-64 build. Allocation and
root performance therefore need a new series, not an assertion of zero cost.

Current field-access migration map (not a declaration of concurrent safety):

| Storage | Current Protector | Required Parallel Boundary |
| --- | --- | --- |
| RootCell/deque/free/count | Owning Attachment only; collector under transitional admission while owners are frozen | Epoch-acknowledged immutable root extents; owner-only writes and completed-epoch resume |
| ThreadRecord registry/arena ID | Transitional admission mutex | Coordinator membership/epoch gate and collector record leases |
| Active Frame/repr/call depth/handled view | Invoking Attachment, existing active roots | Owner-only execution state; safe external snapshots/STW observation |
| heap map/layout checks/accounting | Transitional admission mutex | Stable owner/layout metadata, local allocation/accounting, short slow-path coordination |
| header.type/Cell/instance tail slots | Transitional admission mutex | Rooted atomic edges or short object locks; load-to-root before safepoint |
| Type namespace/MRO/name/flags/epoch | Transitional admission mutex | Frozen topology, TypeSnapshot/current publication and consistent publication sequence |
| Function code/defaults/name/globals metadata | Transitional admission; Code itself immutable | Rooted FunctionCallState snapshot, separate globals-object synchronization |
| Dict entries/index and mappingproxy view | Transitional admission mutex | Callback-free short commit, rooted reads/versioning; Type proxies follow current snapshot |
| Exception args/context/cause/traceback and tb_next | Transitional admission mutex | Rooted field snapshots and coherent chain mutation; identity preserved |
| Numeric/string/tuple/native callable buffers | Logically immutable value/call data, public T& still exposed | Safely published const views, mutable cache rules and audited construction-only access |

The table and new header comments expose current limitations. Full per-field
encapsulation, immutable snapshots and publication are still N0/N2 audit gates;
version counters do not legalize reading concurrently changing STL containers.
No new unprotected ObjectOps parallel entry is enabled by moving roots.

Current happens-before evidence is still mutex-based: owner writes precede
NativeWait unlock; collector admission acquires that mutex before scanning all
frozen arenas; owner reacquires it before resuming. Registration/retirement also
occur under that gate. This is NOT the plan's request/ack/completed protocol.
The next task must replace it with the explicit multi-mutator state machine,
shared admission/resume epoch gate, pinned records and allocation-free-loop
polling, while rejecting unsafe unconverted ObjectOps paths. TransferRecord,
NoSafepoint rooted loads and stable heap metadata remain necessary dependencies.

Existing tail-mutation/repr NativeWait tests now establish receiver-local roots
from values kept continuously rooted/frozen by the sender until the receiver
finishes. This bounded test handoff is not a public queue/TransferRecord API.
The old serial-permission test uses an atomic counter and is explicitly named
as transitional admission, not a requirement that execution stay serialized.

## N1 Multi-Active Storage And Epoch Rendezvous

This supersedes the first root-arena slice's transitional execution mutex.
The production heap supports multiple Active OS threads with private arenas;
synthetic immutable layouts exercise it without a second collector or a
test-only permission mode. Full N1 and Python parallel ObjectOps remain open.

The State coordinator owns phase, registration/retirement and resume admission.
ThreadRecord state is Active, Parked, NativeWait, Collector or Exiting after
registration; PendingAttach cannot enter the registry until phase Running.
The request epoch is atomic, while ack/completed and transitions are ordered
under the same coordinator gate. Root push/pop/publication remain owner-only.

1. The requesting Active owner becomes Collector and publishes a new request
   epoch. Other Active threads poll, publish ack and become Parked. NativeWait
   owners already have frozen roots; Exiting owners keep their registry/State
   leases until the current round finishes. Collector waits release the gate.
2. After no other Active owner remains, membership cannot change until Running.
   The collector scans every frozen arena and the heap, without acquiring a
   heap/object mutex, calling Python or following edges during physical destroy.
   Trace/destroy must not reenter the runtime or coordinator diagnostics.
3. Completion is published under that gate. Resuming and newly attaching threads
   must acquire it and recheck Running; they cannot escape a newer request.
   Multiple explicit collectors join a real collection and receive its result
   through their own record, even if another epoch starts before they resume.

Happens-before: owner root/frame writes precede its ack/state release and gate
unlock in the wait. The collector acquires that gate and observes no Active
owner before reading roots. Its completed epoch and heap changes precede gate
unlock; each owner acquires the same gate before setting Active and accessing
payloads again. NativeWait entry/exit, retirement and new admission use the same
ordering. No owner can mutate a frozen root extent while the collector scans.

`poll` compares request with owner-local observed epoch on its fast path. The VM
polls every 64 dispatch iterations and at reason transfers; allocation also
polls before construction. gc.disable suppresses automatic collection, not
explicit GC or stop acknowledgement. A nonallocating native poll loop is tested;
same-Function/Code nonallocating VM progress is still an N3 gate, not inferred.

`stats` can request a diagnostic stop to read all arena/heap counters safely.
A diagnostic stop does not satisfy a concurrent explicit collect. That join
originally returned zero without reclaiming garbage; the focused regression
failed before distinguishing collection requests from diagnostic requests.
`Interpreter::gc_state` reads only coordinator-owned native state and counts;
it does not inspect an Active arena. Rendezvous duration is recorded separately
from mark/sweep duration, with last/max wait values, not pause percentiles.
Per-thread last-safepoint/resource and timeout diagnostics remain to be built.

Current field migration map (not a declaration of parallel ObjectOps safety):

| Storage | Current Protector | Remaining Boundary |
| --- | --- | --- |
| RootCell/deque/free/count | Owner writes; epoch-frozen collector reads | Bounded pre-reserved NoSafepoint load-to-root API |
| ThreadRecord registry/arena ID/status | Coordinator gate; native record leases | Full thread/shutdown and timeout diagnostics |
| Active Frame/repr/call depth/handled view | Owner-only, traced through existing roots | External reflection snapshots; N3 parallel VM proof |
| Heap map/layout/accounting | Short central heap_mutex; erase only at STW | Stable domain/layout metadata, local spans/budgets and exit reclamation |
| Header/Cell/tail/Dict/Type/Function/exception mutable data | Explicit core quarantine | N2 rooted atomic reads, short locks and immutable snapshots |
| Immutable native buffers | Owner construction and externally synchronized publication | Const access API and audited cache writes |

The temporary heap map returns references to immutable layout elements: rehash
does not invalidate element references, and erase waits for this reader's stop
acknowledgement. Map lookup/registration, shared pressure and external accounting
are locked; no GC rendezvous or Python callback runs while holding that lock.
It can still allocate/wait and serialize common paths, so it is a prototype
bottleneck, not the plan's final scalable allocation protocol.

Bootstrap requires a single registered attachment before enabling quarantine.
The prior concurrently-started bootstrap fixture relied on the old execution
lock; it now tests already-published seed identity under sequential admission.
Separate tests require unsafe parallel bootstrap/admission to fail explicitly
without publishing a partial image. This boundary must be removed through N2,
not renamed as completed shared-object synchronization.

Tests cover 2/4 Active storage workers, shared continuously rooted immutable
input, pressure/explicit GC, simultaneous collectors, admission and NativeWait
return during a request, allocation-free polls, diagnostic joins, closing and
exit acknowledgement. Native storage cost probes retain fixed total work for
1/2/4 workers and verify post-GC live sets. Neither proves Python CPU speedup.
TransferRecord, NoSafepoint/shared reads, local allocation, N2 snapshots and
same PythonFunction/Code execution remain the immediate dependencies. The full
N0-N6/M0-M9 goal is active and no whole concurrency milestone is complete.
