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
present. Complete Python 3.12 builtins are not: print/open/input, bytes/set/super,
compile/eval/exec and remaining constructors/protocol matrices are still work.
Complete their native dependencies and fixed-reference tests before copying
dependent libraries wholesale. `__builtin__` is not the Python 3 module name;
the target is `builtins` and the `__builtins__` execution namespace.

The configured source-tree stdlib path is development configuration, not final
installation, isolation, environment or embedding acceptance. Full stdio, runpy,
-m, REPL and the requested standard-library dependency closures remain in Plan 2.
