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
- importlib.machinery suffix metadata and all_suffixes now share the actual
  FileFinder policy lists: .py sources, the native extension ABI suffixes and
  no bytecode suffixes. DEBUG/OPTIMIZED_BYTECODE_SUFFIXES alias the empty bytecode
  list rather than advertising unsupported CPython .pyc loading. This closes the
  real inspect source-discovery dependency without changing inspect's policy.
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

## Network Policy

- socket.py, selectors.py and socketserver.py are unchanged pinned CPython 3.12.14
  sources. Their TCP/UDP/Unix workflows, stream wrappers and selector/server policy
  call actual native _socket/select capabilities, not Python-library workarounds.
- Native socket/address handling adapts socketmodule.c; select follows
  selectmodule.c and fileobject.c. Precise resource roots, native fd duplicates,
  post-STW disposal and short per-resource commits replace the RC/GIL lifetime
  assumptions. OSError now has the real TimeoutError/ConnectionError subclasses
  and corresponding errno dispatch; socket.timeout aliases canonical TimeoutError.
- This remains an experimental POSIX base, not all of upstream socketmodule.c.
  Ancillary messages/array, sendmsg/recvmsg, interface APIs, all service/protocol
  lookups, IDNA host encoding, complete numeric getnameinfo validation, exact
  diagnostics, Windows/native-extension and Android device acceptance remain.
  Unsupported APIs are absent, so unchanged upstream capability branches remain
  honest. Address resolver failures use actual gaierror/OSError classes.
- Reinitializing a live socket closes its old descriptor instead of reproducing
  pinned CPython's overwrite leak. Finalization follows tracing-GC resurrection
  rules, not refcount timing or repeated native-subtype finalization behavior.
  Concurrent mode changes share the kernel open-file description just as dup/fromfd
  do; a compound sequence of mode changes and I/O is not an atomic transaction.

## Builtins And Product

- Structural patterns follow pinned Python/compile.c and Python/ceval.c protocol
  ordering, using native typed AST/CFG and precise traced operand-stack roots
  instead of CPython's AST/bytecode layout and RC. Mapping get/rest, class
  instance-check/attribute errors, exact __match_args__ tuple/str restrictions,
  builtin self-match and wildcard sequence callback elision have fixed-reference
  script coverage, including adapted Lib/test/test_patma.py cases. Binding-set
  checks precede private-name mangling. Sequence/mapping flags are atomic and
  inherited/ABC-propagated, not inferred through isinstance or __getitem__.
  The generated grammar's ensure-real/imaginary and f-string restrictions are
  enforced in the adapter/scope pass; removed C actions do not broaden accepted
  executable syntax. This slice does not complete the whole upstream test suite,
  type/annotation scopes, or other remaining Python 3.12 gates.

The canonical builtins module/dictionary, namespace lookup and core bootstrap are
present. Base slices now include print, bytes/bytearray, complex, set/frozenset,
super, namespace/reflection, exec/eval compilation/execution, sorting, reverse
iteration, enumerate/zip/map/filter and common aggregate builtins. Complete Python
3.12 builtins, compiler flags/modes and constructors/protocol matrices are still work.
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
- round uses ordinary __round__ descriptor dispatch and the shared numeric
  payloads, including custom callbacks, optional precision and subtype results.
  Int results preserve exact-int identity when no rounding is needed; negative
  precision bounds its power-of-ten exponent by the operand before allocation.
  Float rounding uses an exact binary rational, ties-to-even and correctly rounded
  decimal-to-binary conversion, not std::round's different tie rule. Signed zero,
  NaN/infinity, conversion failures and collection have direct fixtures. The
  extreme-integer-precision VM fixture checks bounded completion where the pinned
  3.12 implementation attempts an infeasible power; no general numeric/performance
  milestone is declared from these tests.
- Builtin compile/eval/exec reuse the existing compiler and VM. Ordinary module
  names now emit namespace instructions, matching separate globals/locals
  behavior; explicit global still emits global instructions. Current valid slices
  include exec/eval/single, supported future flags/dont_inherit and the independent
  0x200/0x4000 codeop input flags. Parser-only flags do not become co_flags.
  Source-to-AST and AST-to-Code now share the ordinary ASDL/IR compiler path.
  Closure execution, path-like filenames, bytecode optimization modes
  and complete diagnostics are not done.
  Complete them in the compiler rather than making Lib permanently avoid them.
- `Lib/codeop.py` and `Lib/code.py` are unchanged pinned sources. The compiler
  now consumes lexer tokens lazily, distinguishes source single-input from
  prompt input, and preserves earlier grammar failures over later lexical ones.
  Literal samples from upstream test_codeop.py, the independent flag/mode matrix,
  future flag memory, real-stdin consoles, hooks and exception recovery have
  direct product tests. KeyboardInterrupt is an ordinary canonical BaseException
  subtype, not a console-only sentinel. The explicit POSIX signal slice and its
  remaining blocking-I/O/startup gates are described below.
- `Lib/traceback.py` retains pinned formatting policy with one private adaptation:
  `_get_code_position` indexes actual VM instructions, not CPython two-byte
  wordcode. Code.co_positions/co_lines and readonly Frame.f_lasti expose actual
  immutable program positions and the atomic frame cursor. Focused exception-only,
  traceback and console cases pass. Native AST and Unicode dependencies are now
  present; complete reflection/Unicode-caret/upstream-suite coverage remains a
  dependency. frame.clear releases completed locals and this frame's Cell edges
  without emptying externally shared closure Cells. Running frames reject clear;
  ordinary suspended generators/coroutines reuse close's actual execution claim
  and finally/GeneratorExit protocol. No per-generator weakref/back-reference or
  new lifetime manager is added. Unraisable cleanup reports the originating Frame,
  not CPython's generator object; async-generator clearing remains an explicit
  experimental finalizer-hook gate. f_locals and full reflection remain work;
  no fake modules or fabricated co_code are introduced to hide them.
  This runtime's standalone generator Frame drops its local edges immediately on
  clear. Pinned CPython retains generator-owned locals after clear until ownership
  transfers and the Frame is cleared/destroyed. The earlier release is recorded
  in Tests/vm_cases/frame_clear_generator_lifetime; common fixtures compare cleanup
  and eventual weak callbacks, not that private refcount-dependent retention.
- `Grammar/Python.asdl`, the upstream ASDL reader, `Lib/ast.py` and `Lib/inspect.py`
  are copied from pinned 3.12.14. The generated native schema is the sole AST
  field/type authority. The CST-to-ASDL and ASDL-to-IR adapters replace the former
  direct CST-to-IR path. Public parse/dump/walk/visitors/transformers/literal_eval/
  unparse and actual compile(AST) have direct source/reference tests. Managed
  conversion honors attribute callbacks, list synchronization, mutable AST class
  identity, native constants and explicit positions. No unparse-to-source compile
  fallback or missing-module policy edits exist. The explicit PyCF_TYPE_COMMENTS
  flag uses the shared lexer/PEG/CST pipeline for assignment, loop, with,
  function/argument comments and Module.type_ignores; default parsing ignores
  those comments. Historical feature_version grammars, complete AST constructor/
  validation/module-lifecycle
  and upstream test matrices remain acceptance work. The current verified VM's
  positive-source-line requirement rejects non-positive AST lines with a tested
  ValueError; CPython accepts them. This explicit representation limit, like the
  native parser/IR depth bounds, remains work rather than full AST compatibility.
- Code argument counts, keyword-only/positional-only counts and local count expose
  actual SignatureLayout/local metadata. dis.COMPILER_FLAG_NAMES names the public
  trait bits already used by VerifiedCode; it does not introduce wordcode.
  Unchanged inspect runs Python function/bound-method signatures and binding,
  annotations, documentation, wrapping and cached-source workflows. Complete
  native callable text signatures, code replacement, frame APIs and inspect's
  remaining matrices are not claimed.
- `Lib/dataclasses.py` is unchanged CPython 3.12.14 source under the retained PSF
  license, not a native replacement. Its decorator/code generation, fields,
  inheritance, frozen/order/hash, InitVar/ClassVar/KW_ONLY, conversion, replace,
  slots and weakrefs use ordinary runtime protocols. Getset/member descriptors
  expose precise owner/name metadata and a shared first-successful __qualname__
  cache. Function globals/builtins/closure are readonly member descriptors,
  distinct from getsets; unchanged types.py therefore classifies inherited slots
  correctly. type(name, bases, namespace) fills a missing __module__ from current
  globals without mutating the input namespace. Full upstream dataclasses
  tests and unsupported serialization dependencies remain acceptance work.
- sys.float_info is a readonly native tuple record backed by actual float.h
  values, with all eleven fields, tuple protocols and sequence construction.
  Its metadata/repr helpers are shared with version_info, not a partial namespace
  containing only max_10_exp. sys.getrecursionlimit reports the Interpreter's
  actual configured logical-call budget (currently 128 by default), not CPython's
  default value. Dynamic limit setters and complete sys/platform records remain
  separate capabilities.
- Native unicodedata adapts pinned immutable Unicode 15.0.0 tables and the 3.2.0
  change records, numeric/type tables and named sequences. Property/default/name/
  lookup APIs, NFC/NFD/NFKC/NFKD and is_normalized use per-call scratch and bounded
  polling. The legacy UCD object uses actual versioned data, not the current
  database under an old label. Stable bounded-class ordering and append-only
  composition avoid repeated whole-string erases. Fixed upstream-derived cases,
  long combining runs and shared-heap GC exercise the product; complete upstream
  Unicode conformance/performance/platform matrices remain acceptance work.
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
  pickle, process/fork facilities and complete pathname matrices remain work.

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
  management, byte-strided slices, native one-element formats
  b/B/h/H/i/I/l/L/q/Q/n/N/P/e/f/d/?/c (including @ prefixes), indexed/slice writes
  and metadata. Element counts and byte offsets/strides remain distinct; scalar
  packing is alignment-safe and uses Python conversion protocols. Comparisons
  compare unpacked values across formats; byte-format hash restrictions remain.
  Export tokens keep native storage alive without accessing managed objects at
  destruction, while live views trace exporters. Bytearray resizing is forbidden
  while exported. Multidimensional casts/shapes, generic exporters and
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
  pinned CPython 3.12.14 sources. Their exported aliases, dynamic class preparation,
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
  protocols do not establish the still-missing typing closure.
- `type Alias = expr` lowers through a typed AST and CFG
  to a lazily evaluated native `_typing.TypeAliasType`; direct construction,
  alias reflection and unions are also implemented. Class-local evaluators can
  see the class namespace. Successful values are atomically cached, but parallel
  first evaluations can run more than once and failures are not cached. VM
  missing-name errors carry NameError.name. Native alias construction,
  reflection and operators adapt CPython 3.12.14 Objects/typevarobject.c under
  the retained PSF license; refcount/GIL storage is replaced by traced edges
  and rooted atomic cache publication.
- `_typing` exposes native TypeVar, ParamSpec, TypeVarTuple, ParamSpecArgs,
  ParamSpecKwargs and Generic. TypeVar/ParamSpec/TypeVarTuple instances support
  attributes and weakrefs despite hiding the `__dict__` descriptor; ParamSpec
  views support weakrefs but not instance dictionaries. TypeVar lazy bound and
  constraints evaluators are traced and successful results use atomic traced
  caches. Generic function, alias and class lowering uses a separate
  AnnotationRole parameter scope: defaults evaluate outside it, annotations
  inside it. Five VM operations create parameters and generic bases and attach
  function parameters: MAKE_TYPE_VAR, MAKE_PARAM_SPEC, MAKE_TYPE_VAR_TUPLE,
  MAKE_GENERIC_BASE and SET_FUNCTION_TYPE_PARAMS.
- Generic class compilation and actual typing/re/warnings imports now run.
  Generic classmethod policy and base construction delegate to
  real `typing` imports, as do non-None native TypeVar bounds, substitution,
  unions and TypeVarTuple unpacking. Missing imports propagate instead of using
  fabricated typing policy. Unchanged Lib/typing.py now has direct reference
  scripts for legacy/syntax generic classes, aliases, ParamSpec/TypeVarTuple,
  origin/args and get_type_hints. This is not complete upstream typing,
  runtime protocol/inspect, serialization or concurrent Python-policy acceptance.
  Omitted/None bounds in the public ParamSpec constructor normalize to NoneType;
  syntax-created ParamSpecs retain the reference's None bound instead.
- Warning and the ten builtin warning categories use ordinary native exceptions.
  Unchanged Lib/warnings.py uses its upstream pure-Python backend: filtering,
  capture, warn/warn_explicit, regex rules, registry invalidation, formatting and
  real frame/stacklevel/skip-file behavior have reference scripts. The two
  upstream backends differ in private registry key layout; tests compare public
  emission/invalidations rather than claiming C-backend internal identity.
  Concurrent catch_warnings is not made thread-local by this port. Native warning
  emission, parser/conversion warnings, -W options and full upstream tests remain.
- sys.version_info is a native readonly tuple subtype, including named fields,
  repr, comparisons/hash and constructor/subclass restrictions. version and
  hexversion report the pinned language baseline, while version identifies
  cxxpython explicitly; this does not claim CPython ABI or a completed language.
  warnoptions starts as the actual empty option list; CLI -W is still a gate.
  Tuple count/index perform normal identity/equality/index protocols with clipped
  bounds and GC-safe callbacks, including subclasses. Single-star subscripts
  construct tuple indexes, not an otherwise invalid standalone StarredExpr.
- Lib/linecache.py and Lib/tokenize.py are unchanged pinned sources. The previous
  deferred import _tokenize adaptation is removed, restoring upstream import
  timing. Python retains encoding-cookie/BOM detection, open/cache/loader-source
  policy, TokenInfo/exact_type, ENCODING emission, untokenize and public error
  translation. Native Modules/_tokenizemodule.cpp now supplies TokenizerIter over
  the compiler's shared syntax::TokenStream, whose state machine adapts pinned
  Parser/tokenizer.c without CPython object APIs. The bridge adapts extra-token
  kinds, source text and character positions rather than treating private compiler
  tokens or byte spans as already public-compatible.
- TokenizerIter is lazy: construction does not invoke readline; refill occurs
  during next() as needed. The text-reader route requires str; the encoding route
  requires bytes, decodes with the selected codec and replacement errors, then
  supplies strict UTF-8 to the native stream. Reader StopIteration and empty input
  signal EOF; other callback/codec errors propagate and stream unwind exhausts
  further iteration. Per-stream immutable native line snapshots carry revisions;
  a traced atomic managed-line cache avoids whole-line conversion for each token.
  A per-iterator claim rejects overlapping/reentrant next(), including callbacks,
  while independent streams remain independent. Lexer polling, rooted callbacks,
  native storage accounting and existing I/O parking replace GIL/RC assumptions,
  not Python-visible reader/error timing. No host interpreter is invoked.
- SyntaxError construction and str adapt pinned Objects/exceptions.c with traced,
  independently writable/deletable msg, filename, lineno, offset, text,
  end_lineno, end_offset and print_file_and_line fields; changing args does not
  recreate those fields. Compile/eval/exec and _imp source errors use the shared
  native conversion, while the tokenizer bridge consumes LexicalDiagnostic and
  preserves the distinction between location fields and location in args.
  Lib/tokenize.py retains upstream SyntaxError-to-TokenError policy. Fixtures for
  tokenize core/readers/errors/f-strings/threads and SyntaxError fields exist;
  their presence is not a passing result. Complete upstream token/position/error
  matrices, warnings, encoding/loader closure, concurrent reader/GC acceptance,
  product diagnostics and measured line-cache/large-input scaling remain gates.
- Lib/string.py is byte-for-byte pinned upstream source, with no temporary Python
  adaptation. Template policy uses re and ChainMap; Formatter.parse/get_field
  call the two native _string factories, while argument numbering, conversions,
  nested specs and vformat remain Python. Modules/_stringmodule.cpp directly
  adapts MarkupIterator/FieldNameIterator from pinned
  Objects/stringlib/unicode_format.h. Exact str and str subtypes use their real
  immutable payloads; Unicode decimal indexes and malformed-input advancement
  retain the upstream parsing contract. Precise source edges and atomic cursor
  claims with periodic polling replace RC/GIL ownership. Private captured native
  types are immutable, non-subclassable and have no instance dictionary or public
  construction path. The existing native str.format renderer remains separate;
  parser unification/parity is not claimed. Formatter/Template and lazy retry
  fixtures are reference inputs, not proof of passing product tests. Complete
  their real dependencies and upstream/concurrent/GC/performance acceptance
  without rewriting string.py to bypass failures.
- Lib/textwrap.py is also unchanged pinned source. Its wrapping, dedenting,
  indentation and shortening policy runs on the actual regex/string APIs.
  General str.expandtabs uses upstream character-column and CR/LF rules, normal
  index conversion and C-int overflow behavior, exact-str identity where valid
  and base-str results for subclasses. Scans and expanded tab fills poll while
  retaining the immutable source across collection. Selected upstream samples
  cover these APIs; this is not a claim of the complete upstream test suite.
- Lib/base64.py is unchanged pinned source. RFC Base16/32/32hex/64, URL/alternate
  alphabets, ASCII85/Base85 options, malformed inputs and legacy stream/bytes
  helpers exercise the real binascii/struct/re closure. bytes/bytearray upper and
  lower use ASCII mapping, independent snapshots and base-family results.
  int.from_bytes/to_bytes use native arbitrary-precision bit import/export,
  Python __bytes__/iteration/index/truth protocols, endian/sign defaults and
  subclass construction. Signed boundaries, zero length, empty input, strided
  memoryviews, GC inside callbacks and larger round trips have reference tests.
  The Boost 1.74 empty-range import defect is avoided by keeping the already-zero
  result, not by reading an invalid zero-limb backend. Full integer diagnostics,
  generic buffer exporters and native signal interruption remain gates.
- VM dis resolves fast-local names through _opcode.local_names rather than
  indexing the cell-filtered co_varnames with the VM's internal slot number.
  Real generic wrapper graphs exercise these otherwise sparse slot indices.
- Lib/re's five Python files are unchanged pinned CPython sources. Native _sre
  adapts the original C matching/validation algorithms, not another regex dialect.
  Pattern/Match/scanner, replacement templates/callbacks, named groups,
  lookarounds, backreferences, atomic/possessive repeats, empty-match advancement,
  Unicode/ASCII/locale bytes modes and buffer subjects have focused reference
  scripts. Pattern instructions are immutable; match captures are immutable with
  a rooted atomic regs cache. Native C scratch is per operation and exception-free
  across its ABI; the attached caller handles GC polling and propagation.
- The general iter(callable, sentinel) clears atomic input edges on sentinel or
  StopIteration, preserving other errors/retry. Its ordinary iterable identity
  is reused by finditer. str.count, str(buffer, encoding, errors), int buffer/base
  parsing and bytes/bytearray translate/maketrans close actual upstream re
  dependencies. Full re, warning/deprecation, native-format/exporter, pickle,
  mutation and diagnostic matrices are not inferred from those passing slices.
- mappingproxy is constructible over actual mappings, not just exact dicts.
  Reads, length, iteration, views/get/copy, comparison and repr delegate to the
  underlying object, including dict subclass overrides. It remains readonly but
  live. Complete mappingproxy operator and diagnostic matrices remain work.
- Ordinary non-generic function annotations evaluate in the defining scope
  after defaults and before closure publication. __annotations__ is a
  traced atomic dictionary edge supporting lazy empty values, replacement, None
  reset and deletion. Variable annotations now follow pinned compile.c/symtable.c
  rules for simple/complex targets, no-RHS effects, local binding, global/nonlocal
  restrictions and prologue mapping setup. Class annotation reflection follows
  typeobject.c's own-namespace and descriptor rules, using existing metadata CAS
  publication rather than inheriting another class's annotations.
- `Lib/__future__.py` is unchanged pinned CPython 3.12.14 source. Annotation
  postponement uses a native AST unparser adapted from Python/ast_unparse.c,
  including precedence, literals, lambda/comprehension and f-string formatting,
  not token joining or source slicing. Temporary annotation scopes do not leak
  runtime closures. Supported compile flags/dont_inherit, source exec/eval and
  REPL input preserve effective flags; precompiled Code does not inherit again.
  co_flags reports actual runtime traits. Barry comparison syntax, other compile
  modes/options, the complete upstream annotation suite, generic type-parameter
  scope and native TypeVar/typing acceptance remain gates. The -i after -m
  future-flags handoff is also not delivered. No upstream annotation was stripped to import
  these files.
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
  _set_sentinel releases the most recent registered lock after Attachment roots,
  Frames and allocation ownership retire. Its bounded precise lease keeps that
  lock alive until replacement/exit, rather than CPython's weak RC timing.
  Per-Interpreter stack configuration uses real pthread attributes. The pinned
  3.12 no-argument stack_size call also resets the setting; this quirk is tested.
  Independent Interpreters allow detached workers and are not subinterpreters.
  Signals/interrupt_main, counts and complete daemon/embedding shutdown remain
  delivery work.
- Unchanged pinned threading.py, queue.py and _threading_local.py run actual
  Thread lifecycle, exception hooks, synchronization primitives, timers, local
  dictionaries and queue/task accounting. CLI shutdown calls an already loaded
  threading._shutdown before atexit, without importing it unnecessarily. Python
  local and SimpleQueue are the upstream fallbacks, not C++ policy rewrites;
  native _local cleanup timing and C SimpleQueue reentrancy are not claimed.
  Trace/profile setters, fork integration, complete shutdown and full upstream
  thread/queue suites remain gates. difflib.py and pprint.py are also unchanged
  pinned sources with direct actual-behavior fixtures for upcoming unittest.
- The native time slice provides actual realtime/monotonic/performance/process/
  thread clocks and nanoseconds, clock_getres metadata and GC-aware sleep.
  Conversion errors, signed/invalid timeouts and progress while another thread
  sleeps use direct tests. POSIX sleep now checks pending Python signals on EINTR
  and resumes the same monotonic deadline. The calendar slice below adds actual
  POSIX conversion/formatting; other platforms are not implied. Native C
  qualified argument-diagnostic names remain
  a known shared reflection gap: get_clock_info(BytesIO()) currently says
  BytesIO instead of CPython's _io.BytesIO. A separate VM fixture records it;
  remove that difference when canonical native diagnostic names are available,
  rather than adding a time-specific class/module-name classification.
- `_abc` reload preserves its private data type and epoch state; `_thread` reload
  preserves lock type identity. Existing classes/functions and instances continue
  to use the same state, rather than constructing incompatible native types on
  every execution of the initializer.

## Unittest And Native Policy Dependencies

- Unchanged pinned unittest, argparse, gettext, fnmatch, locale, shutil and glob sources
  retain Python policy. Core TestCase/Suite/Loader/Runner, discovery, CLI failures
  and catchbreak use actual runtime dependencies. Copying mock.py and async_case.py
  does not complete their unfinished asyncio and other dependency suites.
  locale currently uses upstream's C-locale fallback, not a native _locale backend.
  glob's wildcard/hidden/recursive/root_dir/bytes/symlink paths use the real
  directory primitives; fd-relative globbing is not complete.
- filter reuses the existing immutable child/predicate iterator layout. The child
  iterator owns cursor synchronization; no filter lock spans Python predicates.
  None/bool optimization, lazy errors/retry, subclasses, reduction and collecting/
  parallel callbacks are ordinary builtin behavior, not unittest special cases.
  str.isupper/islower/istitle use the pinned Unicode property tables.
- Native errno supplies platform constants/errorcode. os.access uses faccessat,
  including dir_fd/effective_ids/follow_symlinks; supported capability sets expose
  only implemented variants. terminal_size is a readonly tuple record and
  get_terminal_size uses a real ioctl, not environment guesses or fixed values.
- sys.audit/addaudithook keep an Interpreter-rooted private hook list. Ordered
  hooks and exceptions execute with no callback-spanning lock; adding a hook emits
  the upstream veto event. Explicit audit and Python-policy events work. Complete
  native import/open/exec event coverage and tracing hooks remain gates; this is
  not a security or sandbox boundary.
- _signal owns one process signal lease, with process-lifetime lock-free native
  notification cells and precisely rooted Interpreter handlers. Only the physical
  main OS thread installs/dispatches Python handlers; VM safety points and EINTR
  sleep checks deliver ordinary Python exceptions. Other threads still run Python
  simultaneously. Independent Interpreters may import/read signal information but
  may not replace an active owner's handlers. Destruction restores dispositions
  still owned by that lease, without overwriting host replacements.
- Handler state survives deleting/reimporting _signal in this runtime; pinned
  CPython 3.12 resets that module state. This intentional native-resource ownership
  difference is tested separately in Tests/vm_cases/signal_module_lifetime, not
  hidden in common-reference fixtures. First-worker import/embedding shutdown,
  wakeup-fd, masks/timers, fork/platform coverage and interruption of locks, general
  I/O and readline remain unfinished. The CLI does not unconditionally activate
  the signal module before those blocking paths are integrated.
- sys._stdlib_dir and prefix/base-prefix/exec-prefix fields reflect explicit CMake
  resource configuration, not an inferred host CPython or virtual environment.
- os.scandir publishes an eager native name snapshot with an atomic iterator
  cursor, and closes the directory in NativeWait before managed publication.
  DirEntry has immutable name/path/inode/type hints and precisely traced atomic
  first-success stat/lstat caches; callbacks never run under a directory lock.
  This base slice uses O(directory-size) native memory, retained until iterator
  collection even after close, unlike CPython's streaming DIR handle. fd-based
  scandir, native scandir audit events and full live-directory/platform matrices
  remain gates. Resource tests currently use Linux /proc/self/fd.

## Logging And Pathname Closure

- Lib/logging/__init__.py is unchanged pinned 3.12.14 source. Actual hierarchy,
  levels, propagation, filters/adapters/factories, formatter styles/timestamps,
  stream/file handlers, error handling and concurrent collection have fixtures.
  unittest.assertLogs/assertNoLogs run real logging, not a test-only replacement.
  logging.config, optional handler families, sockets/processes and full upstream
  suites remain dependencies rather than a full logging compatibility claim.
- time.struct_time is a nine-element tuple with readonly native member fields and
  precise tm_zone/tm_gmtoff tails. Construction from sequences/iterables and the
  optional dictionary, equality/hash, repr/reduction, GC cycles and mutable type
  attributes use ordinary object contracts. No parallel struct-sequence engine
  or Python-implemented calendar facade was added. Native field indices follow
  the frozen layout's actual names, not the input order of __slots__.
- POSIX localtime/gmtime/ctime/asctime/mktime/strftime/tzset provide real libc
  behavior, including negative fractional timestamp floor, index callbacks, field
  bounds and normalization, DST and native zone/offset metadata. strftime follows
  the pinned literal-segment algorithm for embedded NUL/non-ASCII/surrogates.
  Only owned native data crosses NativeWait. The existing POSIX environment mutex
  coordinates process TZ state; no interpreter execution lock covers callbacks.
  Timezone module fields are separate snapshot assignments, not a multi-key
  transaction. Native time.strptime imports/calls the unchanged Python
  _strptime._strptime_time through ordinary rooted import and call boundaries.
  Non-C locale policy, complete reload/reflection and other-platform matrices
  remain acceptance work.
- Native functions now have a real readonly __name__; full native __qualname__,
  module/self/text-signature and method-descriptor reflection remain gates.
  Attribute diagnostics use immutable declared native module metadata rather
  than class-name special cases. Index callback errors include the actual result
  type; tuple/string index and rejected-final-base diagnostics follow the pinned
  semantics. This does not imply every historical native diagnostic is complete.
  Ordinary native method-descriptor rejection now names method/owner/receiver
  for explicit calls and binding, with pinned-reference cases for str.format,
  dict.get, list.sort and missing self. Native slots currently share the
  MethodDescriptorType representation: distinct wrapper_descriptor reflection
  and its slot-specific error wording, such as str.__mod__, remain separate
  compatibility gates rather than a reason to special-case method names.
  Mutable number/sequence slot installation is also incomplete: after a list
  subclass overrides __add__/__mul__, assigning back or deleting the override
  does not reproduce CPython's generic numeric-dispatch history. This difference
  was reproduced before protocol-cache optimization. Completing it requires
  per-type slot-update semantics, not class-name/method-name patches; it remains
  a language-compatibility gate while the current cache retains native-definition
  roles. The retained reproduction is .cache/protocol-role-before-restore.py.
- os.cpu_count returns the POSIX online logical count, or None on query failure.
  It does not report affinity/quota or add the later process_cpu_count API to the
  3.12 target. os.symlink accepts/evaluates target_is_directory on POSIX; actual
  dir_fd variants are still separate capabilities.
- Unchanged pathlib/ntpath and urllib/__init__.py, urllib/parse.py and ipaddress.py
  retain path, Windows-pure-path and URI/IP policy. Real filesystem reading/
  writing, globbing, links/rename, URI quoting, shared path caches and concurrent
  collection have direct fixtures. Full pathlib methods still need their actual
  POSIX dependencies, fd handling and platform/error matrices; copying source
  does not make all filesystem operations implemented.
- Native math supplies constants, scalar decomposition/predicates/rounding,
  trigonometric/hyperbolic/exponential/logarithmic and gamma functions, plus exact
  isqrt/gcd/lcm/factorial/comb/perm. Huge integer logarithms round the native
  mantissa instead of overflowing a preliminary float conversion. Rounding uses
  ordinary special methods; int reuses its exact identity conversion for
  __trunc__/__floor__/__ceil__, including huge values and native subclass contents.
  Float exposes the same protocols with arbitrary-precision results and
  NaN/infinity errors. Native integer work owns its operands in NativeWait.
  Native fsum/prod/sumprod/hypot/dist now retain accurate summation/scaling,
  exact integer and generic numeric-protocol behavior, iterator errors and length
  checks, with collecting callbacks and shared-worker fixtures. Remaining math
  APIs include pow/atan2, isclose, erf/erfc and nextafter/ulp;
  complete accuracy/error/performance matrices are still acceptance gates.

## Random And Date Policy

- Lib/random.py, datetime.py, _pydatetime.py, calendar.py and _strptime.py are
  unchanged pinned CPython 3.12.14 sources. Date/time policy uses upstream's real
  pure-Python fallback, not a fake _datetime accelerator. ISO parsing, arithmetic,
  timezone/offset/fold behavior, timestamp conversion, calendars and strptime use
  normal runtime operations. Full locale, platform/DST, upstream-suite and native
  acceleration/performance acceptance remain work.
- _random.Random adapts CPython's MT19937, integer/hash seed conversion, 53-bit
  random, arbitrary getrandbits and native state tuples. Owned native state has
  one per-instance mutex; only native buffers/state are touched in NativeWait.
  Python callbacks, rooted publication and allocation stay outside that mutex.
  Zero bits returns zero without consuming state or passing an empty range into
  the native bit importer. Fixed seeded sequences and state restoration, shared
  draws and collection have executable fixed-reference fixtures.
- os.urandom uses the real Linux getrandom interface with short-read/EINTR and
  pending-signal handling; SystemRandom consumes these bytes through upstream
  Python policy. Default MT seeding also requests actual system entropy. Native
  entropy errors propagate rather than selecting a deterministic seed. Non-Linux
  entropy paths, fork reseeding/at-fork registration and full process lifecycle
  acceptance remain dependencies. MT19937 is not a cryptographic generator.
- Native RNG calls synchronize their own state, not the interpreter. Multi-draw
  Python policies and gauss's documented cache are not atomic transactions across
  threads. Upstream selection, weighted/count sampling, distributions, subclass
  overrides, seed versions and SystemRandom policy remain Python implementations.
- str.removeprefix/removesuffix operate on codepoints, preserve unchanged exact
  string identity and return base strings for subclasses without invoking their
  conversion/indexing overrides. Empty affixes, surrogates/NUL and descriptor/
  argument errors have direct fixtures; the bytes/bytearray counterparts remain
  part of the unfinished primitive-method matrix.

## Pickle And Native Buffer Exports

- Lib/pickle.py, _compat_pickle.py and encodings/raw_unicode_escape.py are
  unchanged pinned 3.12.14 sources. Actual dump/load and file interfaces,
  protocols 0-5, alias/cycle memo handling, reducers/state/newargs, persistent IDs,
  buffer callbacks and error paths execute as Python policy. The native _pickle
  module supplies PickleBuffer, not a fake or partial C++ serializer accelerator.
  Full upstream suites, historical malformed-stream matrices and every native
  object's reduction remain acceptance work.
- PickleBuffer and memoryview use the existing native storage leases. Direct
  exports pin the actual exporter and data owner separately; memoryview.release
  rejects live direct exports. Raw views preserve obj identity, readonly state
  and native-format storage; noncontiguous raw extraction raises BufferError.
  Weakrefs and immediate release, callback/GC/I/O and shared exports have fixtures.
  Independent view copies retain their normal parent-release behavior. Full
  multidimensional/general-extension/PEP 688 support remains a requirement.
- SRE operations and scanners retain the acquired exporter/data roots and lease,
  including mutable versions after the original PickleBuffer is released. They
  do not reacquire a released wrapper or silently freeze mutable subjects.
- Native callables expose actual names/qualified names, module/self metadata and
  ordinary name/getattr reductions. Module references are explicit traced atomic
  fields associated at native initialization, not inferred from global aliases.
  Ordinary type methods use owner-bearing descriptors; native static/class
  wrappers unwrap a rooted read without changing their stored callable. Full
  builtin-method type/reflection and cold initializer/reload matrices remain work.
- raw-unicode-escape supplies the real protocol-0 dependency, including streaming,
  incremental boundaries, errors and collecting replacement callbacks. Buffer
  concatenation follows bytes/bytearray families rather than __bytes__ coercion;
  constructors retain numeric/hook precedence. Float accepts actual contiguous
  buffers and bytearray subclasses use their actual type name in repr. These fix
  primitive seams, not pickle-specific input branches.

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
  NotImplementedError instead of claiming fork safety. The upstream threading
  layer now uses this native recursive lock; full fork/trace/shutdown matrices
  remain a distinct acceptance gate.
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
  complete pathlib/glob capability matrices remain.
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

The pure-Python Lib/json package is copied unchanged from CPython 3.12.14.
The upstream optional _json acceleration remains absent; no alternate parser or
serializer is substituted. Direct acceptance covers nested/Unicode/surrogate
roundtrips, formatting, collecting hooks, errors/cycles, stream interfaces and
shared default encoder/decoder use across OS threads and collection. This is an
experimental pure-Python closure, not complete upstream JSON test acceptance or
a claim that the native accelerator/private APIs are implemented.

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
