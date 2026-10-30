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
3.12 builtins are not: open/input, filter and other remaining
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
  fixed-reference tests. Weakrefs are integrated; pickle and generic aliases
  remain dependencies;
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
  not a declaration of complete genobject.c parity. Async generators
  and the complete frame/code introspection API remain dependencies, not claimed
  generator completion.
- Native coroutines now reuse Frame claims/delegation for async def and await,
  with distinct coroutine and await-wrapper types, return/throw/close and
  cross-thread suspension handoff. Async generators, async for/with and the
  never-awaited warning policy remain unfinished; no fake asyncio is provided.
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
- `Lib/runpy.py` copies the pinned _run_code, _get_module_details,
  _run_module_as_main and _get_main_module_details flows. It deliberately omits
  public run_module/run_path and their helpers until temporary-module/filesystem/
  io/os closures are complete. The warnings branch retains its lazy import;
  warnings/RuntimeWarning integration is still a dependency, not a deleted branch.
  Restore omitted upstream helpers/APIs once those dependencies land.
- Modules/main.cpp and Python/pythonrun.cpp provide the experimental product
  entry/REPL, with a thin Programs entry. Single mode uses live sys.displayhook
  and real SystemExit. Host input/diagnostics are bootstrap bridges, not full
  sys.stdin.readline, readline/history, signals, environment/isolation
  or installed-runtime compatibility. No host CPython executes guest code.
- Bootstrap sys._StandardTextIO is an explicit temporary native stream type,
  using borrowed descriptors and UTF-8 unbuffered writes. Replace this bootstrap
  layer with the real _io/Lib io closure after mutable buffers, raw file resources,
  incremental codecs and configuration exist. Do not claim it is TextIOWrapper or
  copy a fake Python io facade around it. print itself follows current sys.stdout
  and ordinary file.write/flush callbacks; closed-pipe handling does not install a
  process-global signal handler. Input/open and full stream configuration remain.
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
- Native posix exposes descriptor I/O, cwd/listdir, path mutations, stat/lstat/
  fstat, readlink, process IDs and environment primitives. Its readonly stat_result
  uses native tuple fields; OSError carries errno/filename and specific subclasses.
  dir_fd/follow_symlinks variants, complete stat-result reflection/pickle and the
  Python os/pathlib/io closures are not claimed by this native slice.

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
  collecting. Async for/with, asynchronous finalizer hooks, event-loop shutdown
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
  Bound methods forward missing attributes to the wrapped function. Full method
  reflection, comparison and the distinct CPython native-callable type matrix
  remain broader type-compatibility work.
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
- This is not completion of `io`: buffered reader/writer/random/pair, text
  wrappers/decoder cookies, full pickling/reinitialization/diagnostic matrices,
  warning policy and installed stream configuration remain. The existing
  bootstrap sys streams still live in `_io/stdio.cpp`; no fake `Lib/io.py`, open
  builtin or host-Python stream is installed to bypass those dependencies.
- `_thread` implements allocate_lock/LockType, blocking/nonblocking/timed acquire,
  cross-thread release, context management, identifiers, start_new_thread and
  exit. Waiting uses NativeWait; launched Python executes in the same Interpreter
  and shared heap. Startup uses Transfer admission, not a detached borrowed
  Interpreter reference. Locks and interpreter leases retain only their actual
  lifecycle. Python failures are reported, not silently discarded.
  RLock/local, signals/interrupt_main, thread sentinels/count/stack configuration,
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
  properties. Full upstream unittest suites and functools.singledispatch's
  weakref/typing closure are not delivered merely by successful imports.
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

## Requested Library Delivery

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
