# CPython Adaptation Ledger

The language/library reference is the retained CPython 3.12.14 source under
`.cache/deps/Python-3.12.14`. Its license is retained in
`doc/licenses/python-3.12.14.txt`. Prefer copying implementations, then replacing
only the runtime dependencies that actually differ. Do not silently discard
failure paths, reduce public semantics or call a name-only module Supported.
Focused native tests and fixed-reference executable differential tests are the
acceptance evidence; this list is not a generated support matrix.

## Import Policy

- `Lib/importlib/_bootstrap.py`: ModuleSpec, module_from_spec and _find_spec use
  the corresponding upstream definitions. PathFinder and _NamespacePath derive
  from upstream `_bootstrap_external.py`. The existing C++ per-module transaction
  replaces CPython's import-lock/RC mechanisms, not its module table or public
  loader protocol. Precise roots, GC-aware parking and independent leases must
  remain when the remaining upstream policy is ported.
- Namespace path publication uses one immutable state tuple and a fresh epoch
  identity, rather than independently mutating CPython's path/epoch fields under
  GIL assumptions. List snapshots and the published path list use existing
  per-object synchronization. These are required no-GIL adaptations, not a
  reason to change parent-path refresh, mutation or package search behavior.
- FileFinder currently probes files directly, using caller-provided suffixes and
  loader factories. Port upstream directory/mtime/case caches once stat/listdir,
  set/string APIs and platform path behavior are ready; preserve package/module/
  namespace precedence and invalidate_caches semantics. Its full upstream API,
  Windows case behavior and bytecode-only loaders are not complete.
- Source/extension loaders use this compiler and versioned native extension ABI,
  not CPython code bytes or its C ABI. Bytecode caching stays absent rather than
  manufacturing CPython-compatible .pyc paths. Add a documented private cache
  format only with compiler/version validation and the corresponding tests.
- Source file decoding is still UTF-8/BOM with universal newlines. Complete
  bytes/buffer and codecs/encodings before replacing this with the upstream
  encoding-cookie and source decoding implementation. Do not treat a non-UTF-8
  source failure as final Python incompatibility policy.
- Warnings/verbose import diagnostics, importlib.metadata integration, deprecated
  load_module compatibility and resource readers remain dependencies. Preserve
  their upstream branches when their actual implementations and closures land;
  do not add fake warning/metadata/resource modules to make imports pass.
- util.find_spec preserves parent-import errors, ModuleNotFoundError.name and
  exception chaining from upstream. ImportError construction uses traced native
  fields for name/path/msg/name_from. Runtime error support must grow with the
  copied source instead of deleting these error paths from Lib.

## Builtins And Product

The canonical builtins module/dictionary, namespace lookup and core bootstrap are
present. Base slices now include print, bytes/bytearray, complex, set/frozenset,
super, namespace/reflection, exec/eval compilation/execution, sorting, reverse
iteration, enumerate/zip/map and common aggregate builtins. Complete Python
3.12 builtins are not: filter and other remaining
iterators, compiler flags/modes and constructors/protocol matrices are still work.
Complete their native dependencies and fixed-reference tests before copying
dependent libraries wholesale. `__builtin__` is not the Python 3 module name;
the target is `builtins` and the `__builtins__` execution namespace.

- Bytes construction and numeric/aggregate/super implementations adapt the pinned
  bytesobject.c, bltinmodule.c, longobject.c, floatobject.c and typeobject.c flows
  to typed roots and immutable snapshots. Complete bytes methods, mutable buffer
  APIs still need their actual dependencies beyond the buffer/codec slice below.
- Set/frozenset adapt pinned setobject.c's construction, hashing, relation and
  algebra behavior. Private dictionary storage replaces the RC/GIL-dependent set
  table without bypassing hash/equality callbacks or adding another lifetime
  authority. Operation-specific set/dict fast paths, iterable early termination,
  native subclasses, reflected operators and mutation failures have focused and
  fixed-reference tests. Weakrefs and generic aliases are integrated; pickle
  remains a dependency;
  this is not completion of all common type/library integration or performance
  acceptance. Iteration order is not the reference's private table layout.
- Generators adapt pinned genobject.c and PEP 380/479 behavior to the existing
  precise Frame/VM rather than borrowing CPython's reference-count/GIL-dependent
  interpreter frames. Explicit send/throw/close, delegation, return values and
  lazy generator expressions have focused and fixed-reference tests. Frame-state
  CAS replaces GIL serialization; only suspension boundaries permit ownership
  transfer. Automatic collection-time close now uses the common GC finalizer
  queue; delegated close-attribute lookup errors use sys.unraisablehook. The execution claim
  also covers throw argument preparation, so gi_running is already true inside
  a custom exception constructor. These edges need explicit compatibility work,
  not a declaration of complete genobject.c parity. Async generators now have
  the separate implemented slice below; their shutdown/finalizer hooks and the
  complete frame/code introspection API remain work.
- Native coroutines now reuse Frame claims/delegation for async def and await,
  with distinct coroutine and await-wrapper types, return/throw/close and
  cross-thread suspension handoff. Async with uses the same Frame/unwind path,
  awaiting both entry and cleanup, including exceptions and nonlocal exits.
  aiter/anext use actual special-method lookup; the default-bearing anext wrapper
  has precise edges and a per-object execution claim. Async for and async
  list/set/dict/generator comprehensions now reuse that same delegation, including
  eager outer-iterator acquisition, implicit child awaits and cross-thread
  handoff. Never-awaited warnings and event-loop shutdown remain unfinished;
  no fake asyncio is provided.
- Complex construction/mixed arithmetic/hash, imaginary literals and numeric
  formatting adapt the pinned native contracts. Bytearray construction, mutation,
  slices, iteration and decoding use native synchronized storage, not a bytes
  alias. Complete buffer formats/exporters and all bytes-family methods remain.
- Stable sorting and reversed/enumerate/zip/map close real Python library
  dependencies. List sorting roots detached data and detects callback mutation;
  no storage lock surrounds callbacks. These operations do not claim whole-call
  atomicity across arbitrary callbacks. Tuple concatenation/repetition and
  str.split/rsplit retain ordinary subtype/index/Unicode behavior.
- Builtin compile/eval/exec reuse the existing compiler and VM. Ordinary module
  names now emit namespace instructions, matching separate globals/locals
  behavior; explicit global still emits global instructions. Current valid slices
  are exec/eval/single, zero flags and non-positive optimization. AST/future flags,
  closure execution, path-like filenames and complete diagnostics are not done.
  Complete them in the compiler rather than making Lib permanently avoid them.
- Native Unicode interpolation/prefix methods now close actual library dependencies
  instead of rewriting runpy's messages. Percent interpolation, str.format field/
  conversion/nested-spec behavior and startswith/endswith use ordinary typed
  protocols and callback error paths. Complete Unicode APIs and exact malformed
  diagnostic matrices remain broader acceptance work.
- `Lib/runpy.py` is now the complete, unchanged pinned source, including public
  run_module/run_path and temporary-module/argv helpers. run_module has executable
  namespace, initialization-global, package/relative-import and failure-restoration
  tests. run_path still needs actual pkgutil/marshal/import-cache dependencies;
  the warnings/RuntimeWarning branch is retained with its lazy import. Source
  completeness does not declare those remaining paths compatible.
- Modules/main.cpp and Python/pythonrun.cpp provide the experimental product
  entry/REPL, with a thin Programs entry. Single mode uses live sys.displayhook
  and real SystemExit; source input and REPL lines use actual sys.stdin methods,
  including replacement streams. Execution and finalization flush through Python
  protocols; final flush failure returns 120. Emergency native diagnostics/banner
  remain host bridges. Full signal handling, environment/isolation and
  installed-runtime compatibility remain work. No host CPython executes guest code.
- The temporary sys._StandardTextIO implementation is deleted. Standard streams
  are real TextIOWrapper instances over buffered FileIO, borrowing descriptors
  with closefd=False. POSIX defaults are UTF-8, surrogateescape for stdin/stdout
  and backslashreplace for stderr, with terminal/stderr line buffering. Missing
  descriptors become None. Configured stdlib loading precedes user path[0], so
  cwd io.py/encodings do not replace startup dependencies. Locale/environment
  configuration, reconfigure and the complete shutdown sequence remain work.
  print and input use the Interpreter's original sys dictionary, even after
  sys.modules['sys'] is replaced/deleted. Input follows ordinary prompt, flush,
  readline, EOF and error behavior. GNU readline now handles eligible actual
  terminals for both input and REPL, while redirection/replacement stays on the
  Python stream path. Audit and complete terminal/signal policy remain unfinished.
- sys.exception()/exc_info() inspect the active handled exception, including
  its currently synchronized traceback rather than a frozen raise-time copy.
  Nested handlers/calls, finally, generators and coroutine handoff have differential
  tests. The original sys dictionary is a precise Interpreter root, not a second
  module registry; reimported sys remains a distinct module without stdio state.
- sys.excepthook/unraisablehook use real stderr and exception chains. _weakref.ref,
  Python __del__ and gc collection controls use tracing-GC lifetime rather than
  reference-count timing. Weak callbacks retain their strong callback edge after
  clearing targets, as in upstream cyclic collection. Full gc statistics/debug
  APIs and interpreter-shutdown phases remain dependencies. Weak proxies are
  described with the collection dependency closure below.
- Lib/stat.py, keyword.py and token.py copy complete pinned upstream modules.
  stat retains its optional _stat accelerator import and genuine Python fallback;
  no fake native module is installed. Provenance is recorded at each file head.
- Lib/bisect.py, heapq.py and operator.py also copy complete pinned modules,
  retaining their optional native accelerators and real Python fallback. The
  bisect upstream corpus and heapq doctests run through the product. Full upstream
  unittest suites still depend on unfinished unittest/collections/etc.
- Native _operator supplies the actual index kernel needed for special-method
  lookup, exact-int normalization and error behavior. Other operator functions
  remain in the unchanged upstream Python implementation; this is not a complete
  _operator accelerator backend.
- Native posix exposes descriptor I/O, cwd/listdir, path mutations, stat/lstat/
  fstat, readlink, process IDs and environment primitives. Its readonly stat_result
  uses native tuple fields; OSError carries errno/filename and specific subclasses.
  stat/lstat accept dir_fd, stat accepts follow_symlinks and file descriptors,
  and chdir/fchdir are present. Only implemented capabilities are advertised to
  os through _have_functions. Other *at variants, complete stat-result reflection/
  pickle, process/fork facilities and the pathlib closure remain work.

## Buffers, Unicode And Independent Libraries

- `Lib/codecs.py` and the selected `Lib/encodings` modules copy the pinned
  upstream files. They retain CodecInfo, incremental/stream classes, alias lookup,
  Python search callbacks and custom errors. The initial closure includes UTF-8,
  UTF-8-SIG, UTF-16/32 and endian variants, ASCII, Latin-1 and 71 table-only
  single-byte encodings. Other multibyte, compression, IDNA and transformation
  codecs remain dependencies, not modules installed solely to pass import.
- `_codecs` uses a per-Interpreter traced registry, not CPython's RC-owned cache.
  Search/error callbacks run without the registry lock. Unregister invalidates
  cache publication by generation, including an in-flight search result. Native
  kernels retain incomplete streaming suffixes, validate custom handler results,
  permit exception.object replacement and implement standard named errors.
  UnicodeTranslateError and its complete public handler matrix remain work.
- `Objects/unicodetype_db.h` is copied unchanged from CPython 3.12.14's generated
  Unicode 15 database under the retained PSF license. Character predicates and
  case/casefold mappings use that table; namereplace reuses the existing native
  compressed names. Full str API/diagnostic coverage remains a separate gate.
- memoryview supports actual bytes/bytearray aliasing, readonly/release/context
  management, byte-strided slices, B/b/c casts, indexed/slice writes and metadata.
  Export tokens keep native storage alive without accessing managed objects at
  destruction, while live views trace exporters. Bytearray resizing is forbidden
  while exported. Multidimensional/native-format casts, generic exporters and
  the complete memoryview reflection/released-hash matrix are not complete.
- Bytes/bytearray find/rfind/index/rindex/count, partition/rpartition,
  strip/lstrip/rstrip and split/rsplit share native algorithms in
  Objects/bytesmethods.cpp. Bounds use index conversion, buffer needles use the
  existing buffer snapshot API, and results preserve the bytes/bytearray family,
  not arbitrary subclasses. These methods are not path-specific substitutions.
  Callback mutation fixtures cover bytearray growth/in-place writes and buffer
  precedence; shrinking/concurrent mutation, remaining bytes methods, exact
  diagnostics and worst-case search complexity still need broader acceptance.
  No raw exported pointer is retained across Python callbacks by these methods.
- `Lib/struct.py` copies the complete upstream facade. `_struct` supplies compiled
  Struct layouts, packing/unpacking, offset/buffer operations and export-retaining
  iterators. Native alignment/endian, integer widths, half/float/double and s/p
  formats have fixed-reference tests. The full upstream invalid-format,
  reinitialization/subclass and error-message matrices remain acceptance work.
- `Lib/hashlib.py` copies the complete upstream policy. `_hashlib` supplies real
  OpenSSL 3 EVP digest/copy/update, SHAKE, PBKDF2 and scrypt; `_blake2` exports
  canonical types using the pinned portable C kernels, with keyed/salted/personal
  and tree parameters. Their original CC0 notices and full license are retained.
  Shared state uses per-object native locks and NativeWait, not a GIL. Optional
  provider aliases/security configurations, file_digest's I/O dependency and
  full upstream test/error matrices remain; importing hashlib is not completion
  of all these APIs or cryptographic validation/certification.
- Native free functions now remain unbound when stored in Python classes;
  descriptor-native methods still bind receivers. This distinction belongs at
  the common call boundary, not in modified Lib aliases. The existing host
  make_native_function default remains descriptor-capable; builtin-function
  __get__ reflection is not yet identical to CPython's distinct native types.

## Type, Collection And Binary Conversion Closures

- `Lib/types.py`, `_collections_abc.py` and `collections/abc.py` are unchanged
  pinned CPython 3.12.14 sources. Actual type aliases, dynamic class preparation,
  abstract collection registration, sequence/set mixins and wrapped mappings
  execute here. This is not a claim that the complete collections package,
  typing, unittest or every types constructor/helper is available. In particular
  types.coroutine's Code.replace dependency, mutable function code
  metadata and the distinct CPython native-callable descriptor matrix remain.
- Native SimpleNamespace owns the ordinary traced instance-dictionary tail;
  kwargs construction, subclassing, repr/recursion, comparison and reduction use
  actual Python protocols. Its dictionary descriptor's exact reflection still
  follows this runtime's getset representation rather than CPython's member type.
  sys.implementation is a real instance with name='cxxpython' and cache_tag=None;
  bytecode caching is not implemented. Full version/hexversion/platform metadata
  remains a sys compatibility requirement, not invented CPython release data.
- Class subscripting honors metaclass __getitem__ before __class_getitem__.
  GenericAlias implements real origin/args/parameters, substitution preparation
  and unpacking hooks, starred iterators, MRO entries, construction and origin
  attribute forwarding. UnionType implements flattening, argument order,
  equality/hash, substitution and checking short-circuits. Parameter discovery
  publishes rooted immutable tuples; custom callback failures propagate. These
  protocols do not provide the still-missing native TypeVar/typing closure.
- mappingproxy is constructible over actual mappings, not just exact dicts.
  Reads, length, iteration, views/get/copy, comparison and repr delegate to the
  underlying object, including dict subclass overrides. It remains readonly but
  live. Complete mappingproxy operator and diagnostic matrices remain work.
- Ordinary function parameter and return annotations evaluate in the defining
  scope after defaults and before closure publication. __annotations__ is a
  traced atomic dictionary edge supporting lazy empty values, replacement, None
  reset and deletion. Variable annotations and postponed/future evaluation are
  not yet delivered. No upstream annotation was stripped to import these files.
- Native async generators distinguish user yields from inner await suspension
  and reuse the existing VM Frame/unwind. asend/athrow/aclose requests preserve
  their generator-running claim across await suspension and reject competitors.
  Tests transfer a suspended request between OS threads in one shared heap while
  collecting. Asynchronous finalizer hooks, event-loop shutdown
  and the full upstream async-generator matrix remain explicit gates.
- binascii supplies base64, hexadecimal, uu, quoted-printable and CRC32/CRC-HQX
  conversions based on the pinned native algorithms. The module consumes shared
  buffer snapshots and canonical Error/Incomplete types; Python-facing binding
  errors remain Python exceptions. Complete native exception mutability and
  argument diagnostic matrices, and the re-dependent Python base64 policy layer,
  are not declared complete. No host codec process or fake re import is used.

## Native I/O, ABC And Thread Foundations

- `Lib/abc.py` is the unchanged pinned CPython 3.12.14 implementation. `_abc`
  implements its eight native functions with traced weak sets and interpreter-
  local invalidation state. Registered classes are not strongly retained;
  metaclass hash/equality, hook failures, cycles and abstract descriptors follow
  ordinary protocols. Complete collections-ABC type flags and the complete
  upstream unittest/inspect closure remain requirements, not declared support.
- Type `__abstractmethods__` changes publish their truth flag with the namespace;
  `object.__new__` enforces that snapshot. `type.__subclasses__` returns actual
  directly registered live types in definition order. Its weak registry reuses
  ordinary dictionaries and conditional deletion; failed bootstrap restores the
  prior graph. Class/static wrapper initialization, reinitialization, wrapped
  metadata and atomic callable reads now support the actual abc implementation.
  Bound methods forward missing attributes to the wrapped function and now have
  actual construction, equality and hashing. Native constructor functions expose
  their owning type as __self__; native free functions and unbound descriptors
  retain their different binding behavior. Full method reflection and the distinct
  CPython native-callable type matrix remain broader type-compatibility work.
  Python wrapper instances are mutable; native image wrappers reject callable
  reinitialization, preserving builtin immutability. They still use this runtime's
  wrapper representation rather than CPython's separate native descriptor types.
- `_io` exposes real FileIO, BytesIO, StringIO and base stream capabilities.
  FileIO supports path-like/custom openers, borrowed descriptors, raw binary
  read/write/readinto, positioning/truncation, iteration and explicit/GC close.
  BytesIO exports actual mutable aliases and forbids writes/close while exported;
  native readinto exports release immediately on return. StringIO implements
  Unicode positions and the five newline modes. Prepared storage growth and
  per-object synchronization preserve shared-heap GC/parallel access.
- `_io` also supplies BufferedReader/BufferedWriter/BufferedRandom/
  BufferedRWPair, IncrementalNewlineDecoder and TextIOWrapper. Buffering,
  partial/nonblocking writes, readinto/read1/peek, seeking, detach and close use
  actual raw-stream protocols. Text streams use the codec registry, incremental
  state, newline translation and decoder-replay tell/seek cookies, not Unicode
  character offsets presented as file positions. Per-stream execution claims
  serialize that stream's state, reject same-thread reentrancy and park waiters;
  Python callbacks run without the state mutex. This is not an interpreter lock.
- Builtins.open and _io.open build the FileIO -> buffered -> optional text chain,
  validate modes/options and close constructed resources on later failure.
  Lib/io.py is now the complete upstream facade, not a bootstrap replacement.
  Text defaults/text_encoding currently choose UTF-8 rather than full locale/
  UTF-8-mode configuration. reconfigure, complete text truncation, nonblocking/
  iteration-cookie, custom-codec and invalid-input matrices, warnings and installed
  standard streams remain acceptance work; existence of text cookies does not
  assert CPython-private cookie encoding compatibility.
- `_thread` implements allocate_lock/LockType, blocking/nonblocking/timed acquire,
  cross-thread release, context management, identifiers, start_new_thread and
  exit. Waiting uses NativeWait; launched Python executes in the same Interpreter
  and shared heap. Startup uses Transfer admission, not a detached borrowed
  Interpreter reference. Locks and interpreter leases retain only their actual
  lifecycle. Python failures are reported, not silently discarded.
  Thread-local storage, signals/interrupt_main, thread sentinels/count/stack configuration,
  threading joins and product shutdown coordination remain delivery work.
- `_abc` reload preserves its private data type and epoch state; `_thread` reload
  preserves lock type identity. Existing classes/functions and instances continue
  to use the same state, rather than constructing incompatible native types on
  every execution of the initializer.

The configured source-tree stdlib path is development configuration, not final
installation, isolation, environment or embedding acceptance. Full stdio/runpy,
REPL/product configuration and requested library dependency closures remain in Plan 2.

## Collection And Callable Dependencies

- `Lib/reprlib.py`, `Lib/collections/__init__.py` and `Lib/functools.py` copy
  complete, unchanged CPython 3.12.14 files. Their optional accelerators remain
  optional. Product fixtures derived from upstream tests exercise recursive and
  bounded repr, namedtuple, Counter, OrderedDict, ChainMap, UserDict/UserList/
  UserString, wrappers, partial/reduce/comparison helpers, LRU/cache and cached
  properties. The weakref dependency is now present; full upstream unittest
  suites and functools.singledispatch's typing closure are not delivered merely
  by successful imports.
- Native itertools supplies chain, repeat, islice, starmap, pairwise, predicate
  iterators, compress, accumulate, batched, count, cycle, product, permutations,
  both combination families, tee and groupby. Mutable iterator state has an
  execution claim, not a mutex held over Python callbacks. tee uses precise
  forward cache nodes that become collectible when all cursors pass them.
  zip_longest, complete reduction/pickle and iterator reflection remain work.
- `_collections` supplies deque, defaultdict, _tuplegetter and _count_elements.
  deque prepares list nodes before short per-object commits; endpoint operations
  remain O(1), indexing O(n), and iteration checks structural versions. Reads
  own rooted snapshots or slots before callbacks. defaultdict retains the
  existing DictObject layout with one precise factory tail slot, not a second
  mapping engine. Native module reload retains canonical types. deque arithmetic,
  complete reduction/pickle and the full invalid-input matrices remain gates.
- `_thread.RLock` tracks the actual owning OS thread and recursion depth. Native
  waits park the attachment without blocking GC. Save/restore supports condition
  semantics, but coordinated process fork is absent: _at_fork_reinit reports
  NotImplementedError instead of claiming fork safety. Full threading remains a
  distinct Python/native dependency closure.
- `_weakref.proxy` uses the collector's existing weak-target/callback storage and
  roots each live referent before forwarding protocols; dead access raises the
  canonical ReferenceError. Proxy types are distinct from ReferenceType and
  unhashable. These primitives do not substitute for Lib/weakref.py.
- Function metadata/default descriptors use atomic precise edges and snapshot
  binding; update_wrapper no longer fails on readonly Python metadata. Code and
  closure replacement remain work. Iterable __slots__, Unicode identifier/
  translation tables and repetition, dict union, divmod and pow are ordinary
  type/protocol operations, not rewrites tailored to library source. Integer
  modular power uses existing BigInt arithmetic and extended Euclid for inverses.
- sys.intern uses a per-Interpreter weak string pool, pruned during sweep; it does
  not permanently retain every interned string. sys.getsizeof reports this
  runtime's storage/accounting rather than invented CPython byte sizes. Frame
  helpers inspect only the calling thread's rooted frame chain; unrestricted
  cross-thread frame-local inspection remains unsupported.

## I/O, OS, Enum And Weakref Source Closure

The following eight added files are complete, byte-for-byte copies of the fixed
CPython 3.12.14 Lib sources, under the retained PSF license. Their syntax, lazy
imports and failure branches have not been removed to make bootstrap succeed:

- `Lib/io.py`: Python stream ABC registration/facade over the real _io types.
- `Lib/os.py`: POSIX platform selection, environment, PathLike and filesystem policy.
- `Lib/genericpath.py`: shared Python path/status policy.
- `Lib/posixpath.py`: POSIX string/bytes path algorithms and the os.path alias.
- `Lib/enum.py`: Enum/IntEnum/StrEnum/Flag/IntFlag, auto and Python metaclass policy.
- `Lib/weakref.py`: weak mappings, WeakMethod and finalize over native weak targets.
- `Lib/_weakrefset.py`: Python weak-set/iteration policy.
- `Lib/copyreg.py`: reduction helpers, dispatch and extension registries.

Full source retention is not full module compatibility. The implementation and
remaining acceptance boundaries are:

- I/O and product: real open/raw/buffered/text construction and resource cleanup
  have targeted stream/mode/newline/cookie/error fixtures. Bootstrap standard
  streams, input, locale/configuration, warnings, reconfigure and full I/O
  protocol/error matrices remain.
- OS and paths: POSIX syscalls, UTF-8/surrogateescape filesystem conversion and
  upstream environment/path policy are present. Cached sys.modules aliases such
  as os.path do not require a package __path__. Full POSIX capability/error
  coverage, filesystem configuration, Windows/other platforms, process/fork and
  dependent pathlib/glob closures remain.
- Enum and type hooks: upstream enum policy uses runtime/core default type docs,
  int.bit_length/bit_count, bin/oct/hex and the original __set_name__ exception
  plus its CPython 3.12 note. The full upstream enum corpus and remaining mixed-
  type/descriptor/formatting/diagnostic cases remain; enum and typing are not
  declared complete.
- Weak containers/finalizers: tracing-GC weak targets/proxies, conditional
  _remove_dead_weakref deletion, upstream WeakMethod/finalize and actual atexit
  are present. Conditional deletion cannot erase a replacement binding. Full
  upstream coverage, compound Python-policy concurrency acceptance and coordinated
  interpreter shutdown remain; collection timing is not CPython RC timing.
- Reduction/copyreg: object.__reduce__/__reduce_ex__/__getstate__, getnewargs
  hooks, dict/slot state and list/dict iterators use traced roots and actual
  copyreg helpers. The full pickle engine/library closure, all native-layout
  reducers, reconstruction/protocol/diagnostic coverage and serialization
  compatibility remain.
- Exceptions and methods: mutable exception dictionaries, add_note/__notes__,
  BlockingIOError.characters_written and bound-method construction/equality/hash
  are present. Full exception/native-callable reflection and invalid-input
  coverage remain; default core docs do not supply every upstream builtin
  docstring.
- Exit callbacks: Interpreter-rooted atexit register/unregister/clear/run state,
  LIFO dispatch, callback exception reporting and CLI post-execution invocation
  supply weakref.finalize's real dependency. Thread joins/admission shutdown,
  final GC/module teardown and embedding/abnormal-exit
  lifecycle integration remain.

Modules/atexitmodule.cpp adapts the pinned callback semantics, not Python object
reference-count shutdown. Registration/deletion use short local commits; equality
and callbacks execute outside those locks. A per-state run claim coordinates
recursive/concurrent runners with NativeWait. Interpreter state, rather than the
module object's reachability, keeps registered callbacks alive. This does not
complete the runtime's full shutdown protocol. The listed targeted fixtures do
not substitute for full upstream suites or final sanitizer acceptance.

## Copy And Native Reconstruction

`Lib/copy.py` is another complete, unchanged pinned CPython 3.12.14 file. Shallow
and deep-copy policy, memo/alias/cycle handling, registered reducers and custom
copy/state hooks remain in Python. Native types supply their actual reconstruction
protocols: set/frozenset and bytearray reducers, tuple/int/float/complex/str/bytes
constructor arguments, and range/slice reducers. Payload snapshots produce exact
builtin argument values without invoking subtype conversion hooks; ordinary
constructors and copied state retain subtype identity and aliases. Mutable
bytearray state callbacks precede its data snapshot, matching the reference.
This is not a pickle engine or full upstream copy/pickle acceptance. Remaining
native resource/iterator reducers, weakref compound-policy concurrency and full
serialization/diagnostic matrices stay separate gates.

## Context Management And Exception Groups

`Lib/contextlib.py` is the complete, unchanged pinned source. Its ordinary and
asynchronous generator managers/decorators, ExitStack/AsyncExitStack, closing/
aclosing, nullcontext, redirectors, chdir and suppress run against real runtime
protocols. Native BaseExceptionGroup/ExceptionGroup supply construction, readonly
message/exceptions, derive/subgroup/split and metadata/notes preservation; custom
derive and error paths remain observable. ExceptionGroup keeps the reference's
mutable heap-type behavior, unlike immutable BaseExceptionGroup. Async with uses
the existing Frame's awaited cleanup, including cancellation, suppression,
return/break/continue and cross-thread suspension handoff. except* uses existing
Except/Finally regions and traced operand stacks rather than a second Frame
state machine: ordered matching, overridden split/derive, naked wrapping,
metadata-aware leaf projection and handler failures compose with suspension.
CPython 3.12's unused C notes field is not the ordinary Python __notes__ attribute;
copying/comparing that attribute must not classify bare reraises as new errors.
Malformed split results receive Python TypeError rather than introducing invalid
handled-exception state. Full upstream suites, group tree diagnostics and
serialization remain work; contextlib import alone is not their acceptance.

## Requested Library Delivery

gc controls and diagnostics use the existing precise shared-heap tracing collector,
not CPython's reference-count/cyclic-GC internals. get_objects/get_referents/
get_referrers freeze via existing epochs and root results before resume; untyped
storage is hidden/transparent, and real header/type edges are included. All boxed
objects are tracked, including leaf numbers/strings. get_stats assigns actual
physical sweeps/reclamation totals to generation 2; generation 0/1 have no objects
or fabricated statistics. collect's generation arguments remain full-heap aliases.
get_count reports allocation pressure, and get_heap_stats exposes native byte,
root, span and pause counters (reclaimed_bytes is the last sweep, reclaimed_objects
is cumulative). gc.callbacks uses the interpreter-owned original list, active
owner-thread Python calls, unraisable reporting and per-owner recursion suppression.
Each outer request has a pair, so concurrent pairs can overlap/join one epoch;
no callback or execution lock serializes the shared heap. Generation thresholds,
debug/save-all, permanent-generation freeze and full upstream tests remain open.

GNU readline supplies real line editing, history mutation/files, completer and
startup/pre-input callbacks, delimiters and line/index helpers. Its private typed
module state traces callback edges and owns native history; a process-local
external-resource claim swaps GNU history rather than copying it on every call.
Whole GNU calls park for GC, including terminal output backpressure; synchronous
Python callbacks resume through NativeCallback without an internal mutex and
refreeze on return/unwind. Session cleanup restores tty and native callbacks/streams, with GNU longjmp
signal handling disabled. CMake detects the external library; no fake readline
module is installed without it. Full display hooks, automatic rlcompleter,
reload/history policy and product signal integration remain dependencies.

Lib/curses is copied byte-for-byte from the fixed reference. Native _curses uses
ncursesw for windows, terminal mode/restoration, narrow/wide input, Unicode/byte
strings with optional coordinates/attributes, drawing/refresh basics, colors
and terminfo. It defines a real _curses.error rather than aliasing RuntimeError.
Opaque native resources and post-STW finalization replace CPython's refcounted
WINDOW disposal; parked calls coordinate the native terminal, not the interpreter.
PTY acceptance covers wrapper exceptions, terminal restoration, blocked input
with concurrent CPU/GC progress, Unicode and nonterminal/unknown-TERM failures.
The complete panel/textpad sources retain their native dependencies and are not
claimed functional: subwindows/pads, panels, resizing, full cell/character methods,
complete parameter/encoding policy and multi-screen sessions remain gates.

opcode/dis adapt inspection policy and public Instruction/Positions tuple
protocols to this runtime's actual verified instruction stream. Recursive listing,
current-offset/traceback selection, constants/names/cells, UTF-8 byte source
columns and actual branch effects are exposed. Numeric offsets are instruction
indexes, not CPython wordcode bytes. Standalone stack_effect rejects call-table
indexes; code-aware Bytecode.stack_effect reads the actual call layout. No
co_code/cache/specialization facade is manufactured. Full upstream dis/reflection
and control-region inspection remain gates. Native str.ljust/rjust/center/zfill
close real formatting dependencies with index conversion, Unicode fills,
sign/parity, overflow and exact-string/subtype identity behavior.

The requested Base-to-Full scope includes dis, collections, typing, types, ctypes,
encodings, logging, multiprocessing, pathlib, re, string, unittest, abc, ast,
base64, code, codecs, contextlib, dataclasses, enum, glob, hashlib, io, opcode,
pdb, os, pickle, queue, socket, struct and threading. Earlier requests also include
sys, posix, curses, importlib, weakref, runpy, tokenizer/tokenize, trace and uuid.
The REPL delivery also requires readline, complete curses, actual-bytecode dis
and tracing-GC-compatible gc inspection/control/statistics, not just imports.
These are delivery requirements, not a claim that the modules already work.

- Close native buffer/resource and codec dependencies first, then upstream
  encodings/codecs/io/os/pathlib/glob and complete runpy. Keep filesystem and
  descriptor capability native, path and library policy in Python.
- Complete native reflection, GenericAlias/Union, coroutine/async-generator and
  class contracts for types/abc; then collections/itertools/functools dependencies,
  contextlib/enum/dataclasses/typing. Do not replace these libraries with C++
  facades to avoid their syntax or metaclass requirements.
- Supply real regex, binascii, hash, struct, thread/queue, socket and foreign-call
  capabilities for re/string/base64/hashlib/struct/threading/queue/socket/ctypes.
  Build logging/pickle/multiprocessing on those dependencies, not on names-only
  modules or a hidden host-Python execution path.
- Connect ast/code/opcode/dis/pdb to this compiler's actual metadata and product
  execution, preserving the independent bytecode and extension ABI contracts.
- Each usable closure runs actual product scripts plus pinned upstream test data
  and doctests where dependencies permit. The complete unittest and upstream
  regression-test package are themselves unfinished closures, not a fake testing
  facade. Host test drivers remain in Tests/python; a future Lib/test is the
  actual adapted upstream Python test package, not host drivers or C++ tests.
- Pure-Python additions rerun their dependency/library tests, not unchanged native
  sweeps. Native type, GC or VM edits rerun the affected contracts and sanitizer
  cases. Preserve valid earlier results instead of restarting unrelated CPU
  performance campaigns for every library import.
