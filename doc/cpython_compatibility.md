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
present. Base slices now include print, immutable bytes, set/frozenset, super, namespace/reflection,
exec/eval compilation/execution and common aggregate builtins. Complete Python
3.12 builtins are not: open/input, bytearray/memoryview, remaining
iterators, compiler flags/modes and constructors/protocol matrices are still work.
Complete their native dependencies and fixed-reference tests before copying
dependent libraries wholesale. `__builtin__` is not the Python 3 module name;
the target is `builtins` and the `__builtins__` execution namespace.

- Bytes construction and numeric/aggregate/super implementations adapt the pinned
  bytesobject.c, bltinmodule.c, longobject.c, floatobject.c and typeobject.c flows
  to typed roots and immutable snapshots. Complete bytes methods, mutable buffer
  exports and the Unicode codec registry still need their actual dependencies.
- Set/frozenset adapt pinned setobject.c's construction, hashing, relation and
  algebra behavior. Private dictionary storage replaces the RC/GIL-dependent set
  table without bypassing hash/equality callbacks or adding another lifetime
  authority. Operation-specific set/dict fast paths, iterable early termination,
  native subclasses, reflected operators and mutation failures have focused and
  fixed-reference tests. Weakrefs, pickle and generic aliases remain dependencies;
  this is not completion of all common type/library integration or performance
  acceptance. Iteration order is not the reference's private table layout.
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
  sys.stdin.readline/sys.excepthook, readline/history, signals, environment/isolation
  or installed-runtime compatibility. No host CPython executes guest code.
- Bootstrap sys._StandardTextIO is an explicit temporary native stream type,
  using borrowed descriptors and UTF-8 unbuffered writes. Replace this bootstrap
  layer with the real _io/Lib io closure after mutable buffers, raw file resources,
  incremental codecs and configuration exist. Do not claim it is TextIOWrapper or
  copy a fake Python io facade around it. print itself follows current sys.stdout
  and ordinary file.write/flush callbacks; closed-pipe handling does not install a
  process-global signal handler. Input/open and full stream configuration remain.

The configured source-tree stdlib path is development configuration, not final
installation, isolation, environment or embedding acceptance. Full stdio/runpy,
REPL/product configuration and requested library dependency closures remain in Plan 2.
