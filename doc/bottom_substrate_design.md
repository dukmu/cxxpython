# Bottom Substrate Design

This document is the execution plan for the next runtime substrate work. The
goal is to keep new Python-visible features on stable object-space
infrastructure instead of accumulating VM, builtin, and parser shortcuts.

## Dependency Order

The next bottom layers should be built in this order:

1. Execution request substrate.
2. Class construction substrate and `__build_class__`.
3. Module registry and import substrate.
4. Closure/cell substrate for `super`, nested scopes, and future generators.
5. Slice substrate and sequence slicing.
6. Byte/buffer substrate for `bytes`, `bytearray`, and `memoryview`.
7. IO substrate for `open`, `input`, stdio, and text streams.
8. Traceback/exception-state substrate.

This order is intentional. Import needs module registry and execution
requests; `super` needs closure context; bytes and memoryview need buffer
ownership; `open` and `input` need runtime-managed IO objects; full traceback
needs frame/code/source metadata and exception state.

## Execution Requests

Current state:

- `compile()`, `eval()`, and `exec()` are wired to the existing compiler and
  VM.
- CLI script execution and REPL execution create `__main__` modules directly.
- Python functions reuse the active VM when one exists.

Target design:

- Introduce a single `ExecutionRequest` value passed to runtime/VM helpers.
- Fields:
  - `CodeObject *code`
  - `DictObject *locals`
  - `DictObject *globals`
  - `DictObject *builtins`
  - `ModuleObject *module`
  - execution mode: eval, exec, module, class body, function
  - filename and display name fallback
- `eval`, `exec`, CLI, REPL, import execution, and class-body execution should
  all construct this request instead of calling VM overloads ad hoc.
- Builtins injection belongs here: if globals lacks `__builtins__`, resolve it
  once through the runtime builtins module and expose the same object shape
  across eval, exec, import, and CLI.

Acceptance tests:

- `eval(expr, globals, locals)` reads locals first and globals second.
- `exec(stmt, globals)` writes into globals and injects `__builtins__`.
- CLI `file.py`, REPL, and `exec(opened module code)` all expose consistent
  `__name__`, `__file__`, and `__builtins__`.

## Class Construction

Current state:

- Class statements now lower through visible `builtins.__build_class__`.
- `Runtime::build_class` owns metaclass selection, `__prepare__`, class-body
  execution, and final metaclass call.
- `Runtime::new_class` remains the low-level allocator used by `type(...)` and
  `type.__new__`.
- `MAKE_CLASS` remains only as a legacy thin bytecode dispatch.

Target design:

- Add a `ClassBodyRequest` or extend `ClassConstructionRequest` with:
  - class body code object or callable
  - class name
  - bases tuple
  - class keyword dict
  - defining module
  - qualname
  - explicit metaclass
- Add a runtime API that owns the full protocol:
  1. select metaclass
  2. call `metaclass.__prepare__(name, bases, **kwds)`
  3. execute class body with prepared namespace
  4. call metaclass as `metaclass(name, bases, namespace, **kwds)`
  5. validate result is a type-like object for class statements
- Expose `__build_class__` in builtins and lower class statements through it.
  The VM should become stack dispatch only; it should not contain metaclass or
  namespace policy.
- Keep `Runtime::new_class` as the low-level type allocator used by
  `type.__new__`, not as the whole class-statement protocol.

Acceptance tests:

- `__build_class__` is visible in `builtins`.
- Class statement invokes custom `__prepare__` before body execution.
- `metaclass=` keyword is honored.
- Metaclass conflicts continue to raise the existing `TypeError`.
- Class decorators still apply after class construction.

## Module And Import Substrate

Current state:

- `ModuleObject` exists and has `__name__`, `__file__`, and `__builtins__`.
- Runtime owns a module registry and search-path list.
- `__import__` is exposed for simple `.py` module names. It searches runtime
  paths, inserts modules into the cache before execution, reuses cached
  modules, and removes failed imports.
- CLI script execution adds the script directory to the search path; REPL adds
  the current working directory.
- There is no script-visible `sys.modules`, package metadata, module spec, or
  package importer yet.

Target design:

- Add runtime-owned module registry:
  - `modules` dict equivalent to `sys.modules`
  - import search path list
  - importing stack for circular import diagnostics and cleanup
- Add typed module metadata:
  - `__spec__`
  - `__package__`
  - `__loader__`
  - `__path__` for packages
- Add minimal importer:
  - resolve absolute module names
  - load `.py` files from search path
  - create module and insert into registry before execution
  - clean failed imports from registry
  - return top-level package for normal `__import__`
- Expose `__import__` only after the registry and failure cleanup exist.

Acceptance tests:

- Repeated import returns the same module object.
- Failed import does not leave a broken entry in the registry.
- Circular import observes the partially initialized module.
- `import package.module` and `__import__('package.module')` return CPython-like
  objects for the supported subset.

## Closure And `super`

Current state:

- `CellObject` exists as a runtime type and participates in traversal.
- Code objects carry `cellvars` and `freevars` metadata.
- Python function objects carry closure cells and traverse them.
- Frames own a combined cell vector ordered as `cellvars + freevars`.
- The VM implements `LOAD_DEREF`, `STORE_DEREF`, `DELETE_DEREF`, and
  `LOAD_CLOSURE`.
- The compiler detects function cell/free variables, including free-variable
  pass-through across intermediate nested functions.
- `global` and `nonlocal` statements are parsed and analyzed for normal
  function scopes. Global writes use explicit global opcodes; nonlocal writes
  use closure cells.
- Nested functions capture shared mutable cells, so mutation after inner
  function creation is visible through the closure.
- `super` is now a real builtin type backed by descriptor and MRO lookup.
- Class bodies can carry `__class__` cells and backfill `__classcell__` so
  zero-argument `super()` and direct `__class__` use inside methods work.
- Symbol analysis now lives in `Python/compiler_symbols.cpp` instead of being
  embedded in bytecode emission.

Target design:

- Extend symbol analysis with:
  - local
  - global
  - nonlocal
  - cell
  - free
- Add `CellObject` and CodeObject metadata:
  - `cellvars`
  - `freevars`
  - closure tuple on function objects
- Add opcodes:
  - load/store/delete deref
  - load class cell
  - make function with closure
- Class body compilation should create `__class__` cell when needed.
- Tighten declaration ordering diagnostics before default arguments or
  comprehensions expand the scope surface.
- Keep descriptor/type attribute policy centralized so `super`, property,
  classmethod, staticmethod, and slot wrappers all share one object-space path.
- Extend `compiler_symbols.cpp` with stricter declaration ordering diagnostics
  before more scope rules and class semantics land.

Acceptance tests:

- Nested function captures outer local. Done for normal function scopes.
- Intermediate nested functions pass free variables through. Done for normal
  function scopes.
- `nonlocal` mutates the nearest enclosing cell. Done for normal function
  scopes.
- `super()` works in a method on a simple inherited class. Done.
- `super(type, instance).method()` binds through descriptors. Done.

## Slice And Sequence Slicing

Current state:

- Negative integer indexing exists for list/tuple and str getitem.
- `SliceObject` exists as the builtin `slice` type.
- `slice(start, stop, step)` and `slice.indices(length)` are implemented for
  integer/None components.
- Parser, AST, compiler, bytecode, and VM support `a[start:stop:step]` through
  `BUILD_SLICE`.
- List, tuple, and str slicing use the ordinary `getitem` slot path.

Target design:

- Add slice assignment/deletion only after list mutation semantics are designed
  for ranged replacement.
- Add `__index__` integration so slice components share the same conversion
  path as integer indexing.
- Add range slicing so `range(...)[slice]` returns a compact range object.

Acceptance tests:

- `slice(1, 5, 2).indices(10)`. Done.
- `[0, 1, 2, 3][1:3]`. Done.
- `'abcd'[::-1]`. Done.
- slice assignment is deferred until list mutation semantics are designed.

## Byte And Buffer Substrate

Current state:

- `bytes`, `bytearray`, and `memoryview` are intentionally absent.
- `int()` already mentions bytes-like input in an error string, but no buffer
  protocol exists.

Target design:

- Add immutable `BytesObject` with contiguous byte storage.
- Add mutable `ByteArrayObject` with contiguous byte storage.
- Add buffer protocol slots:
  - get buffer
  - release buffer
  - readonly flag
  - length
  - item size and format seed
- Add `MemoryViewObject` that references an exporter and keeps it alive.
- Constructors must validate integer-byte range 0..255 and iterable input via
  `object_iter`.

Acceptance tests:

- `bytes([65, 66]) == b'AB'`
- `bytearray([65])[0] = 66`
- `memoryview(bytes(...))` is read-only.
- Invalid byte value raises `ValueError`.

## IO Substrate

Current state:

- `print` writes to a runtime `stdout_stream`.
- `input` and `open` are absent.

Target design:

- Add runtime-managed standard streams:
  - stdin
  - stdout
  - stderr
- Add typed IO object family:
  - raw file object
  - text wrapper with encoding policy
  - simple string-backed stream for tests
- `print(..., file=...)` should call the file object's write method rather
  than writing directly to host streams.
- `input(prompt)` writes prompt to stdout and reads one line from stdin.
- `open()` returns a text stream first; binary mode should wait for byte/buffer
  substrate.

Acceptance tests:

- `input()` reads from test stdin.
- `print(..., file=stream)` dispatches to `write`.
- `open(path).read()` and context manager support after `with` exists.

## Traceback And Exception State

Current state:

- Exceptions can carry one source location seed.
- CLI prints a minimal traceback line.
- There is no linked traceback object or active exception state.

Target design:

- Add `TracebackObject` with:
  - frame
  - instruction index/line
  - next traceback
- Add per-thread exception state:
  - current exception
  - caught exception stack
  - context/cause seed
- VM catch path should attach frames as traceback objects during propagation.
- CLI should print traceback chains from exception objects, not source seed
  fields.

Acceptance tests:

- Exception raised in nested function prints both frames.
- `except Exception as e` preserves the caught exception during handler
  execution.
- Bare `raise` reraises the active exception.

## Immediate Work Queue

1. Extract execution request helpers from builtins/CLI/function call sites.
2. Implement `__build_class__` through the class construction substrate.
3. Add module registry and minimal `sys` module seed.
4. Implement `__import__` with cache and cleanup semantics.
5. Add closure/cell metadata before `super`.
6. Add `slice` object and parser lowering.
7. Add bytes/buffer, then IO.

Each item must land as a vertical slice with C++ tests, Python golden tests,
documentation updates, and a clean full test run.
