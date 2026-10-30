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
  APIs, weak proxies and interpreter-shutdown phases remain dependencies.
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

The configured source-tree stdlib path is development configuration, not final
installation, isolation, environment or embedding acceptance. Full stdio/runpy,
REPL/product configuration and requested library dependency closures remain in Plan 2.

## Requested Library Delivery

The requested Base-to-Full scope includes dis, collections, typing, types, ctypes,
encodings, logging, multiprocessing, pathlib, re, string, unittest, abc, ast,
base64, code, codecs, contextlib, dataclasses, enum, glob, hashlib, io, opcode,
pdb, os, pickle, queue, socket, struct and threading. Earlier requests also include
sys, posix, curses, importlib, weakref, runpy, tokenizer/tokenize, trace and uuid.
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
