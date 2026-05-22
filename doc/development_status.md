# Development Status

## 2026-05-22 While And Boolean Control Slice

Review focus:

- Close the next parser/control-flow gap before growing more script-level
  stdlib.
- Implement Python truth-control semantics without adding unnecessary VM
  state or low-quality special cases.

Completed:

- Added lexer/parser/AST/compiler support for `while` and `while ... else`.
- Added boolean `and` / `or` expressions with short-circuit lowering.
- Preserved Python's value-returning boolean semantics:
  - `a and b` returns the first false operand, otherwise the last operand;
  - `a or b` returns the first true operand, otherwise the last operand.
- Reused the existing loop stack for `break` and `continue`, including
  `while`-`else` behavior where `break` skips the `else` suite.
- Extended Python golden coverage for `while`, `continue`, `break`,
  `while ... else`, boolean short-circuit side effects, and inline parser
  syntax.

Design notes:

- `and` uses `JUMP_IF_FALSE_OR_POP`; `or` lowers to
  `DUP_TOP`, `POP_JUMP_IF_TRUE`, `POP_TOP` so short-circuiting keeps the
  original truthy object on the stack while falsey values are discarded before
  evaluating the next operand.
- `while` uses existing absolute jumps and the same loop control records as
  `for`, so future finally/unwind behavior stays centralized.

Known debt and next risk:

- Augmented assignment remains the next expression/target lowering gap.
- Parser still lacks relative import syntax and package `__path__` multi-entry
  search in import execution.
- Object-layer VM includes remain a design cleanup target.

Verification:

- `cmake --build build -j2`
- `build/cxxpython Tests/python/control_bool.py`
- `build/cxxpython Tests/python/parser_syntax.py`
- `build/test_core_objects '--gtest_filter=PythonScripts.RunsGoldenFiles:Compile.CompilesIfElseStatements:Compile.CompilesChainedComparisonsWithShortCircuit:Bytecode.DispatchesJumpOpcodesThroughTruthProtocol:Bytecode.DispatchesJumpIfFalseOrPop'`
- `ctest --test-dir build --output-on-failure`

Result: all 195 tests pass.

## 2026-05-22 Star Import and `__all__` Protocol Slice

Review focus:

- Turn stdlib `__all__` from passive metadata into real import semantics.
- Keep star-import behavior in parser/compiler/VM import execution, not hidden
  in `__import__` special cases.
- Preserve module-level symbol analysis and reject scope shapes that are not
  implemented safely yet.

Completed:

- Added AST support for `from module import *`.
- Added parser validation that `import *` is only accepted at module level.
- Added `IMPORT_STAR` bytecode metadata and VM dispatch.
- Implemented CPython-shaped star import behavior:
  - prefer module `__all__`;
  - require `__all__` entries to be strings;
  - import each named attribute through normal attribute lookup;
  - when `__all__` is absent, import public module dictionary names and skip
    names beginning with `_`.
- Added Python golden coverage for `__all__` exports, public-name fallback,
  private-name skipping, and invalid nested `import *`.

Design notes:

- `__import__` still returns modules according to the normal fromlist rule.
  The namespace mutation belongs to `IMPORT_STAR`, which has access to the
  executing frame and can update the correct locals dictionary.
- Star import is restricted to module scope for now. That avoids corrupting the
  current symbol-table model for function fast locals and class namespace
  execution.

Known debt and next risk:

- `IMPORT_STAR` currently uses the shared iterable collection helper for
  `__all__`; this is acceptable with today's non-moving allocation model, but
  a future GC pressure slice should replace returned raw-vector helpers with a
  rooted iterator-consumption API.
- Relative imports, package `__path__` multi-entry search, boolean `and/or`,
  `while`, and augmented assignment remain the next parser/import gaps.
- Several object-layer files still include `vm.hpp`; continue moving VM-shaped
  behavior behind Runtime/Python-layer helpers.

Verification:

- `cmake --build build -j2`
- `build/cxxpython Tests/python/import_star.py`
- `build/test_core_objects '--gtest_filter=Bytecode.MetadataNamesAndStackEffects:PythonScripts.RunsGoldenFiles'`
- `ctest --test-dir build --output-on-failure`

Result: all 195 tests pass.

## 2026-05-22 Descriptor Layer Split Slice

Review focus:

- Remove the layering violation where `Objects/object_descriptor.cpp`
  implemented getset descriptor mechanics and also registered object, type,
  code, function, and frame attributes.
- Keep foundational descriptor objects independent from VM/bytecode headers.
- Preserve current script-visible behavior while making the next attribute and
  introspection work less tangled.

Completed:

- Reduced `Objects/object_descriptor.cpp` to the getset descriptor object and
  its slots only.
- Moved runtime registration of `__class__`, `__dict__`, `__doc__`,
  `type.__base__`, `type.__bases__`, `type.__mro__`, `type.__qualname__`,
  code-object attributes, function attributes, and frame attributes into
  `Python/runtime_type_descriptors.cpp`.
- Added the new runtime descriptor translation unit to the core build.
- Verified that descriptor mechanics no longer pull `bytecode.hpp` or
  `vm.hpp` into the descriptor object implementation.

Design notes:

- `GetSetDescriptorObject` is now a reusable object-layer primitive.
  Per-type descriptor registration is runtime bootstrapping policy, not part
  of the descriptor object's storage/dispatch implementation.
- Code/function/frame attributes remain intentionally registered from the
  Python/runtime side because they expose execution objects to the script
  layer and require bytecode/VM layouts.
- This split is behavior-preserving; it is an architecture slice before adding
  richer descriptor and introspection protocols.

Known debt and next risk:

- `Objects/object_core.cpp`, `Objects/object_function.cpp`,
  `Objects/object_method.cpp`, and `Objects/object_super.cpp` still include
  `vm.hpp`. Review each dependency and move VM-specific call/class/super
  behavior behind Runtime or Python-layer helpers where practical.
- Star import lowering still needs to use module `__all__` with CPython's
  fallback behavior for public names.
- Parser syntax still lacks boolean `and/or`, `while`, augmented assignment,
  relative import syntax, and more complete import statement variants.

Verification:

- `cmake --build build -j2`
- `build/test_core_objects '--gtest_filter=Attributes.*:Bytecode.*:PythonScripts.RunsGoldenFiles:Module.*'`
- `ctest --test-dir build --output-on-failure`

Result: all 195 tests pass.

## 2026-05-22 Parser Syntax and Stdlib Export Slice

Review focus:

- Remove parser limitations that were already forcing `Lib/` modules into
  awkward one-line or artificially nested code.
- Add public `__all__` metadata to the current script-level stdlib modules
  without implementing star-import lowering prematurely.
- Record the descriptor layering issue before further attribute/runtime growth.

Completed:

- Added lexer tokens for `elif`, `pass`, and `;`.
- Taught the lexer to suppress physical newline/indent/dedent tokens while
  inside `()`, `[]`, or `{}` so multiline literals work like Python.
- Added AST/compiler support for:
  - `pass`;
  - `elif`;
  - colon-following inline suites such as `if x: pass`;
  - semicolon-separated simple statements;
  - conditional expressions, `a if cond else b`.
- Added script-level coverage in `Tests/python/parser_syntax.py` for `pass`,
  `elif`, inline suites, semicolon-separated statements, conditional
  expressions, class one-line bodies, for-loop one-line bodies, and chained
  comparisons.
- Added `__all__` to `collections`, `io`, `inspect`, `os`, and `posixpath`.
- Extended stdlib golden tests to assert representative `__all__` entries.

Design notes:

- Statement conditions now parse through comparison expressions rather than
  the full conditional-expression entry point. That prevents `if cond:` from
  being misread as the beginning of `x if cond else y`.
- Multiline literal support belongs in the lexer by tracking nesting depth; the
  parser should not consume arbitrary newlines inside expression parsing,
  because that crosses statement boundaries incorrectly.
- `__all__` is now present as module metadata. Star-import is intentionally not
  lowered yet; it needs compiler/import integration and `__all__` fallback
  rules as a separate semantic slice.

Known debt and next risk:

- `object_descriptor.cpp` still mixes the getset descriptor object
  implementation with registration for object/type/code/function/frame
  attributes, and therefore includes VM/bytecode details in an object-level
  file. Split descriptor mechanics from per-type descriptor registration so
  foundational object code does not absorb VM-specific responsibilities.
- Parser still lacks boolean `and/or`, `while`, augmented assignment, star
  import, and relative import syntax/semantics.
- `__all__` is metadata-only until star-import lowering is added.

Verification:

- `cmake --build build -j2`
- `build/cxxpython Tests/python/parser_syntax.py`
- `build/cxxpython Tests/python/sys_os.py`
- `build/cxxpython Tests/python/stdlib_modules.py`
- `build/test_core_objects '--gtest_filter=PythonScripts.RunsGoldenFiles:Compile.CompilesChainedComparisonsWithShortCircuit'`
- `ctest --test-dir build --output-on-failure`

Result: all 195 tests pass.

## 2026-05-22 Package Imports and Script Path Layer

Review focus:

- Continue opening the stdlib/import layer without adding unnecessary native
  modules.
- Make package imports real enough for script-level `os.path` and future
  package-based stdlib modules.

Completed:

- Added package-aware module specs with `package_dir`, `is_package`,
  `__path__`, and `ModuleSpec.submodule_search_locations`.
- Extended runtime import discovery from flat module names to dotted package
  paths:
  - `pkg/__init__.py`
  - `pkg/mod.py`
  - `pkg/mod.so`
- Made `Runtime::import_module("pkg.child")` import and validate the parent
  package first, cache both modules, and bind the child module on the parent.
- Updated parser/compiler import lowering for dotted `import` and
  `from ... import ...`:
  - plain `import pkg.child` binds `pkg`;
  - `import pkg.child as alias` binds the child module to `alias`;
  - `from pkg.child import value` uses a real fromlist.
- Updated `__import__` to follow the CPython return rule:
  - no fromlist returns the top-level package for dotted imports;
  - non-empty fromlist returns the requested module;
  - package fromlist entries are opportunistically imported as submodules.
- Added recursive Python golden discovery so package fixtures under
  `Tests/python/` can be tested without being executed as top-level scripts.
- Added `Tests/python/package_fixture/` and `Tests/python/package_imports.py`
  coverage for package metadata, dotted imports, aliases, from-imports, and
  direct `__import__`.
- Added script-level `Lib/posixpath.py` and exposed it as `os.path`, keeping
  path manipulation in Python instead of adding unnecessary native module
  surface.

Design notes:

- Import behavior now has a single semantic entry through `__import__` and
  `Runtime::import_module`; the compiler no longer relies on one-argument
  `__import__` shortcuts that would block package/fromlist semantics later.
- Package metadata is deliberately minimal but shaped like CPython's public
  objects: `__package__`, `__path__`, `__loader__`, `__spec__`, and
  `submodule_search_locations` are now present where scripts expect them.
- `posixpath.py` uses only currently supported language syntax. This is not a
  compatibility fallback; it prevents the stdlib from depending on parser
  features that are not implemented yet.

Known debt and next risk:

- Parser expression support is still uneven for Lib-quality code: `elif`,
  augmented assignment, boolean `and/or`, and plain comparison expressions in
  `while` conditions need proper grammar/compiler support so stdlib modules can
  be written more naturally.
- Import still lacks relative import resolution, star-import lowering,
  `__all__`, package `__path__` search through multiple entries, and a real
  importlib bootstrap module. These should be added before stdlib grows many
  packages.
- `__import__` currently ignores globals/locals for package context because
  relative imports are still rejected; that should move to a dedicated
  import-state/request object before relative imports land.

Verification:

- `cmake --build build -j2`
- `build/test_core_objects '--gtest_filter=Module.ImportsPackagesAndDottedSubmodules:Module.ImportsStdlibModulesFromLibPath:Compile.AcceptsDottedImports:PythonScripts.RunsGoldenFiles'`
- `build/cxxpython Tests/python/sys_os.py`
- `ctest --test-dir build --output-on-failure`

Result: all 195 tests pass.

## 2026-05-22 Minimal Extension C ABI Slice

Review focus:

- Replace the temporary extension loader's C++ `Runtime&` entry convention with
  a narrow C ABI surface.
- Keep internal C++ classes, `Rooted<T>`, and STL-heavy implementation details
  out of extension modules.

Completed:

- Added public `Include/cxxpython/extension_api.h` with opaque runtime/object
  handles and a small function table.
- Split extension loading into `Python/import_extension.cpp`.
- Changed extension entry points to:
  `CxxPyInit_<module>(CxxPyRuntime *, const CxxPyExtensionApi *)`.
- Added minimal ABI functions for:
  - creating modules
  - creating strings
  - creating integers
  - setting module/object attributes
  - reporting import errors
- Added `Tests/extensions/cxxpython_sample_extension.c`, compiled as a real
  `.so` without linking `cxxpython_core`.
- Added `Tests/extensions/cxxpython_failing_extension.c` to lock failure
  cleanup and the no-throw pending import-error path.
- Added focused C++ coverage that imports the sample extension through the
  normal runtime import path and observes its exported Python attributes and
  `ExtensionFileLoader` metadata.
- Added focused C++ coverage that failed extension initialization raises
  `ImportError` and does not poison `sys.modules`.

Known debt and next risk:

- The ABI has only a minimal no-throw pending-error channel. It still needs a
  richer exception/status model before exposing native callable callbacks.
- The ABI has no reference/rooting model yet. Before exposing richer object
  creation or function callbacks, add explicit handle lifetime/root rules.
- There is no module definition helper, native function binding helper,
  argument parser, or extension type layout API yet.

Verification:

- `cmake --build build -j2`
- `build/test_core_objects --gtest_filter='Module.ImportsExtensionModuleThroughNarrowCAbi:Module.ExtensionImportUsesSharedLoaderAndReportsMissingEntryPoint:Module.ImportsPythonModulesThroughRuntimeRegistry'`
- `build/test_core_objects --gtest_filter='Module.ImportsExtensionModuleThroughNarrowCAbi:Module.FailedExtensionInitializationDoesNotPoisonModuleRegistry:Module.ExtensionImportUsesSharedLoaderAndReportsMissingEntryPoint'`
- `ctest --test-dir build --output-on-failure`

Result: all 194 tests pass.

## 2026-05-22 Import Core Split Slice

Review focus:

- Reduce `Python/import.cpp` from a mixed finder/importlib/source/extension
  implementation into narrower import-core translation units.
- Keep behavior unchanged before adding package imports or a stable extension
  ABI.

Completed:

- Kept `Python/import.cpp` focused on `Runtime::import_module` orchestration
  and `sys.modules` cache handling.
- Added `Python/import_find.cpp` for builtin/source/extension discovery and
  `ModuleLoadSpec` creation.
- Added `Python/import_importlib.cpp` for loader/spec helper object creation
  and module metadata initialization.
- Added `Python/import_load.cpp` for builtin, source, and current `.so`
  extension loading paths.
- Reused the `Modules/module_registry.hpp` builtin/native module registry
  instead of duplicating builtin module-name lists in import code.

Known debt and next risk:

- `import_load.cpp` still contains both source execution and extension loading.
  The next `.so` ABI slice should split extension loading into a dedicated
  file and replace the temporary `CxxPyInit_<module>(Runtime&)` convention with
  a narrow function-table/object-handle API.
- Dotted/package imports are still intentionally unsupported by the public
  `import_module` path. Package discovery, `__path__`, and importlib script
  policy should land as a separate behavior-changing slice.
- Importlib helper objects are still runtime-created heap types rather than
  script-level `importlib._bootstrap` classes.

Verification:

- `cmake --build build -j2`
- `build/test_core_objects --gtest_filter='Module.*:Cli.ImportsSiblingPythonModule:PythonScripts.RunsGoldenFiles'`
- `ctest --test-dir build --output-on-failure`

Result: all 192 tests pass.

## 2026-05-22 Native Module Registry Split Slice

Review focus:

- Continue correcting the CPython-style source boundary after moving native
  substrate code out of `Objects/`.
- Prevent `Modules/config.cpp` from becoming a second monolith.
- Keep cross-module symbols narrow; no public RAII/template-heavy ABI was
  introduced.

Completed:

- Reduced `Modules/config.cpp` to the builtin/native module table and dispatch.
- Added `Modules/module_registry.hpp` as a narrow internal registry boundary.
- Split native module initialization into:
  - `Modules/sys.cpp`
  - `Modules/gc.cpp`
  - `Modules/posix.cpp`
  - `Modules/_io.cpp`
  - `Modules/_collections.cpp`
- Kept thin `_io.TextIOWrapper` layout/slots in `Modules/_io.cpp`; no `_io/`
  subdirectory until the module actually needs one.
- Avoided extra cross-module helper symbols for stdio objects; `sys` reads
  them directly through `Runtime`.

Known debt and next risk:

- `Modules/_collections.cpp` still contains policy-heavy `namedtuple`. Move that
  to `Lib/collections.py` once class/string helpers are strong enough.
- `Python/import.cpp` is now the largest remaining mixed-responsibility file:
  finder/spec logic, source loading, extension loading, and importlib helper
  object creation should be split before package/dotted import support.
- The extension loader still uses a temporary C++ entry point. Next ABI work
  should expose a compact function table/object-handle boundary and keep
  `Rooted<T>`/STL-heavy internals private.

Verification:

- `cmake --build build -j2`
- `build/test_core_objects --gtest_filter='Module.ImportsStdlibModulesFromLibPath:Module.BootstrapsBuiltinsModule:PythonScripts.RunsGoldenFiles'`
- `ctest --test-dir build --output-on-failure`

Result: all 192 tests pass.

## 2026-05-22 CPython-Style Source Layout Slice

Review focus:

- Correct the growing source-layout debt before adding more loader and native
  module support.
- Keep `Objects/` reserved for core object model code instead of using it as a
  bucket for runtime, import, stdlib substrate, and IO implementation.
- Avoid exposing C++ RAII/template-heavy internals across the future extension
  ABI boundary.

Completed:

- Moved runtime bootstrap/accessors/factories/rooting from `Objects/` to
  `Python/`.
- Moved builtin bootstrap, import core, and GC core from `Objects/` to
  `Python/`.
- Moved the native module registry/substrate implementation from `Objects/` to
  `Modules/config.cpp`.
- Moved `_io.TextIOWrapper` implementation from `Objects/` to `Modules/_io.cpp`.
- Updated `CMakeLists.txt` to reflect the CPython-aligned source ownership.
- Updated architecture docs with the rule that extension ABI must not expose
  internal RAII/template types such as `Rooted<T>`.

Known debt and next risk:

- `Modules/config.cpp` has since been split into direct module files such as
  `Modules/sys.cpp`, `Modules/gc.cpp`, `Modules/posix.cpp`, `Modules/_io.cpp`,
  and `Modules/_collections.cpp`.
- `Python/import.cpp` still combines finder/spec logic, source loading,
  extension loading, and importlib helper object creation. Split it into
  smaller import-core files before package/dotted import work.
- `_io.TextIOWrapper` is still declared in the public monolithic
  `object.hpp`. Move module-private layouts behind narrower internal headers
  when the module split settles.
- The `.so` extension entry point still uses a temporary C++ signature. The
  next ABI slice should introduce a narrow function-table/object-handle API
  without exporting internal RAII helpers.

Verification:

- `cmake --build build -j2`
- `ctest --test-dir build --output-on-failure`

Result: all 192 tests pass.

## 2026-05-22 Import Loader/Spec Substrate Slice

Review focus:

- Start moving import toward CPython's loader/spec organization instead of
  hard-coding every source/native path directly in `Runtime::import_module`.
- Keep current stdlib native pieces compiled into the runtime for now, while
  introducing a real extension-module discovery/loading path for future `.so`
  modules.

Completed:

- Added `ModuleLoadSpec` and `ModuleLoadKind` for builtin, source, and
  extension module discovery.
- Routed import through:
  - `Runtime::find_module_spec`
  - `Runtime::load_module_from_spec`
  - `BuiltinImporter`
  - `SourceFileLoader`
  - `ExtensionFileLoader`
  - `ModuleSpec`
- Added module import metadata:
  - `__loader__`
  - `__spec__`
  - `__package__`
  - `__cached__`
- Ensured source modules receive import metadata before executing their module
  body, so module-level code can read `__loader__` and `__spec__`.
- Added a true `.so` loader path using `dlopen` and an explicit
  `CxxPyInit_<module>(Runtime&) -> ModuleObject*` entry point convention.
- Added runtime ownership of successful extension handles so loaded modules
  stay mapped for the runtime lifetime and are closed during runtime teardown.
- Kept stdlib/native modules compiled internally for this stage.
- Added tests for source module loader/spec visibility, builtin module
  loader/spec visibility, and extension-loader failure when a discovered `.so`
  cannot be loaded.

Known debt and next risk:

- `ExtensionFileLoader` has only the discovery/loading skeleton. It still needs
  a stable C/C++ extension ABI, module definition helpers, ownership/rooting
  rules for external code, and reload/unload policy beyond runtime-lifetime
  handle ownership.
- Import still rejects dotted/package names. `__package__` is seeded for the
  future, but real packages require directory discovery, `__path__`, and
  package-relative resolution.
- Loader/spec helper types are internal heap types, not script-level
  `importlib._bootstrap` classes. That is acceptable for the substrate slice,
  but importlib should eventually own the higher-level policy in `Lib/`.
- `runtime_stdlib_modules.cpp` still groups unrelated native modules. Split it
  before growing sys/gc/posix/_io/_collections further.

Verification:

- `cmake --build build -j2`
- `build/test_core_objects --gtest_filter='Module.ImportsPythonModulesThroughRuntimeRegistry:Module.ExtensionImportUsesSharedLoaderAndReportsMissingEntryPoint:Module.ImportsStdlibModulesFromLibPath'`
- `build/cxxpython Tests/python/sys_os.py`
- `build/test_core_objects --gtest_filter='PythonScripts.RunsGoldenFiles'`
- `ctest --test-dir build --output-on-failure`

## 2026-05-22 Inspect Script Layer + sys._getframe Slice

Review focus:

- Continue the CPython native-module inventory correction: `inspect` is a
  Python stdlib module, not a builtin/native module.
- Expose only the unavoidable runtime primitive through `sys._getframe`, then
  keep `inspect` policy in `Lib/inspect.py`.

Completed:

- Removed the temporary native `_inspect` module from the runtime stdlib
  registry.
- Added `ThreadState::frame_at_depth(...)` and native `sys._getframe(depth=0)`.
- Rewrote `Lib/inspect.py` as a script module using `sys._getframe` and public
  frame/code/function attributes.
- Added read-only function descriptors:
  - `__name__`
  - `__qualname__`
  - `__code__`
  - `__globals__`
  - `__module__`
- Added Python golden coverage that `_inspect` is absent from
  `sys.builtin_module_names`, `sys._getframe` works at depth 0, function
  metadata is script-visible, and `inspect.getfile/getsourcefile` use that
  metadata.

Known debt and next risk:

- `inspect.isfunction` currently uses the function type name because the
  script layer lacks a CPython-like `types.FunctionType`. Add a script-level
  `types.py` once native type aliases can be exposed cleanly.
- `sys._getframe` has only depth support. `f_back` is still absent, so richer
  traceback/debugger traversal remains blocked.
- `runtime_stdlib_modules.cpp` now hosts several unrelated native modules
  (`sys`, `gc`, `posix`, `_io`, `_collections`). Split it by substrate before
  the native module surface grows again.

Verification:

- `cmake --build build -j2`
- `build/cxxpython Tests/python/stdlib_modules.py`
- `build/test_core_objects --gtest_filter='PythonScripts.RunsGoldenFiles:Module.ImportsStdlibModulesFromLibPath'`
- `ctest --test-dir build --output-on-failure`

## 2026-05-22 CPython Native Module Alignment Review

Review focus:

- Native module names must follow CPython's actual substrate inventory, not a
  locally invented private-hook naming scheme.
- Public stdlib still remains script-first: `os.py` owns policy, while the
  POSIX substrate exposes host calls.

Completed:

- Checked local CPython's `sys.builtin_module_names`; POSIX is exposed as
  `posix`, `sys` and `gc` are builtin modules, and `_io` remains the native IO
  substrate.
- Renamed the runtime POSIX module from `_posix` to CPython-aligned `posix`.
- Promoted `sys` and `gc` from script wrappers over invented `_sys`/`_gc`
  hooks to CPython-aligned native modules.
- Removed `Lib/sys.py` and `Lib/gc.py`; their state comes from the runtime
  module registry instead of a fake private hook layer.
- Updated `Lib/os.py` to import `posix`.
- Added golden coverage that `sys.builtin_module_names` contains `posix` and
  does not contain `_posix`, and likewise contains `sys`/`gc` rather than
  `_sys`/`_gc`.
- Updated architecture documents with the rule: do not invent underscore
  native module names when CPython already defines the substrate name.

Known debt and next risk:

- `_inspect` was not a CPython builtin module; it has since been removed.
  `sys._getframe` now provides the runtime primitive while `Lib/inspect.py`
  owns policy.
- `_collections` is a CPython native helper name, but current
  `collections.namedtuple` still contains too much policy in C++. Move it to
  Python once class construction and string/list helpers are sufficient.
- `posix` remains intentionally small: `stat`, errno objects, fd operations,
  environment mutation, path-like conversion, and richer `OSError` fields are
  pending.
- `os.path` should come from script package support (`posixpath.py`), not from
  C++.

Verification:

- `cmake --build build -j2`
- `build/cxxpython Tests/python/sys_os.py`
- `build/test_core_objects --gtest_filter='PythonScripts.RunsGoldenFiles:Module.ImportsStdlibModulesFromLibPath'`
- `ctest --test-dir build --output-on-failure`

## 2026-05-21 IO Substrate Seed Slice

Review focus:

- Apply the CPython-like split: bottom objects and host capabilities in a
  private `_io` hook, public organization in scripts.
- Avoid growing `open`, `input`, and `print` as policy-heavy C++ builtins before
  text stream objects exist.

Completed:

- Added a runtime `TextIOObject` backed by host C++ streams.
- Added `_io.TextIOWrapper` type and private `_io` hooks:
  - `_stdin()`
  - `_stdout()`
  - `_stderr()`
  - `open(path, mode='r')`
- Added text stream methods:
  - `read()`
  - `readline()`
  - `write(str)`
  - `flush()`
  - `close()`
  - `closed()`
- Added `Lib/io.py` as the public script-level module exposing
  `TextIOWrapper` and `open`.
- Updated native `sys` to expose `stdin`, `stdout`, and `stderr` from `_io`.
- Added Python golden coverage for `io`, `sys.std*`, file read/write,
  closed-file behavior, and missing-file `OSError`.

Known debt and next risk:

- `print` still writes through `Runtime::stdout_stream()` and only supports
  `sep`/`end`; it should next dispatch through `sys.stdout.write` and support
  `file`/`flush` without embedding stream policy in the builtin.
- Public `open` is available as `io.open`, not yet as `builtins.open`. When it
  moves to builtins, the public wrapper should come from script/bootstrap
  policy rather than expanding `_io` semantics.
- `input()` is still absent. It should be script-level once `sys.stdin` and
  `sys.stdout` can be used reliably from builtins/bootstrap.
- `_io.open` supports only text modes `r`, `w`, and `a`; encoding, errors,
  newline policy, binary modes, buffering, path-like objects, context-manager
  protocol, and finalizer behavior are pending.
- `TextIOObject` currently exposes `closed()` as a method because property
  wiring for native object attrs is still thin. This should become a proper
  descriptor-backed attribute when native getset coverage expands.

Verification:

- `cmake --build build -j2`
- `build/cxxpython Tests/python/io_module.py`
- `build/test_core_objects --gtest_filter='CoreObjects.RuntimeBootstrapsBuiltinTypes:CoreObjects.BuiltinTypesHaveRuntimeDictionaries:Module.ImportsStdlibModulesFromLibPath:PythonScripts.RunsGoldenFiles'`
- `ctest --test-dir build --output-on-failure`

## 2026-05-21 POSIX Hook Split Slice

Review focus:

- Continue the stdlib openness correction by replacing the `_os` pseudo-hook
  with a real POSIX substrate.
- Keep public `os` behavior in `Lib/os.py`.
- Add the exception substrate needed for filesystem errors instead of routing
  POSIX failures through generic exceptions.

Completed:

- Added builtin `OSError` to the exception hierarchy, runtime accessors,
  factories, roots, and builtins module.
- Renamed the native hook module from `_os` to `posix`.
- Moved `os.name`, `sep`, `linesep`, `curdir`, and `pardir` into `Lib/os.py`
  as script-level policy constants.
- Added real `posix` hooks:
  - `getcwd()`
  - `listdir(path='.')`
  - `environ()`
- Mapped `posix.listdir` filesystem failures to Python `OSError`.
- Added Python golden coverage for `os.getcwd`, `os.listdir`, `os.environ`,
  `TypeError` argument validation, and `OSError` missing-path behavior.
- Added C++ coverage that `OSError` is bootstrapped and visible in builtins.
- Fixed an adjacent bootstrap omission: `RecursionError` now receives a type
  dictionary during builtin type initialization.

Known debt and next risk:

- `posix` is still a small substrate. `stat`, `getenv`/`putenv`, errno shape,
  path-like conversion, file descriptor operations, and richer OSError fields
  are still pending.
- `_io` was absent at the time of this slice. `open`, `input`, `sys.stdin`,
  `sys.stdout`, and `sys.stderr` should not expand until raw/text IO object
  types exist.
- Importer still lacks package and dotted-module support, which blocks
  script-first stdlib growth such as `os.path`, `collections.abc`, and
  `traceback`.
- Parser support for boolean operators and some nested call/attribute forms is
  still thin; POSIX tests were kept within the currently supported syntax.

Verification:

- `cmake --build build -j2`
- `build/test_core_objects --gtest_filter='CoreObjects.RuntimeBootstrapsExceptionHierarchy:Module.BootstrapsBuiltinsModule:Module.ImportsStdlibModulesFromLibPath:PythonScripts.RunsGoldenFiles'`
- `ctest --test-dir build --output-on-failure`

## 2026-05-20 Stdlib Openness Review

Review focus:

- Re-check the stdlib foundation against the project rule that public modules
  should be script-first.
- Avoid cementing public API behavior in C++ before POSIX and IO substrates
  exist.
- Prevent fake host integration from becoming test-passing technical debt.

Findings:

- `Lib/sys.py`, `Lib/os.py`, `Lib/inspect.py`, `Lib/gc.py`, and
  `Lib/collections.py` were too thin at this stage. `sys` and `gc` have since
  been corrected to CPython-aligned native modules; `os.py`, `inspect.py`, and
  `collections.py` remain script-level policy surfaces.
- `_os` is the wrong long-term substrate name for host filesystem calls. The
  native layer should become CPython-aligned `posix` on POSIX platforms, with `os.py`
  providing public names and portability policy.
- POSIX is not implemented yet, so exposing placeholder filesystem helpers is
  dangerous. Placeholder `_os._listdir` was removed instead of returning an
  empty list.
- `_io` does not exist yet. `open`, `input`, `sys.stdin`, `sys.stdout`, and
  `sys.stderr` should wait for a real IO object substrate rather than becoming
  string/stream special cases in builtins or CLI code.
- `collections.namedtuple` is still a native helper because the script layer
  cannot yet express the needed dynamic tuple subclass behavior cleanly. This
  is acceptable only as a private bootstrap dependency and should be revisited
  after class/function syntax matures.
- `sys.builtin_module_names` must describe native/builtin modules, not public
  script wrappers.

Corrections made:

- Removed the fake `_os._listdir` native hook.
- Changed the runtime builtin-module inventory away from public script wrappers.
- Updated `architecture_design.md`, `bottom_substrate_design.md`, and
  `development_guide.md` with the script-first stdlib rule and the no-fake-hook
  rule.

Next implementation order:

1. Rename/split `_os` into `posix` and keep `os.py` as the public adapter.
2. Add a real `posix` substrate for `getcwd`, `listdir`, `stat`, `environ`,
   and errno-to-Python-exception mapping.
3. Add `_io` runtime object types before exposing `open`, `input`, stdio, or
   file-backed imports beyond simple source loading.
4. Expand importer package support so stdlib can grow as scripts and packages:
   dotted names, package directories, `__path__`, `__package__`, and later
   `__spec__`.
5. Move more `inspect` and `collections` policy into `Lib/` as parser and
   runtime affordances become available; keep `gc` native because CPython does.

Verification:

- `cmake --build build -j2`
- `build/test_core_objects --gtest_filter='Module.ImportsStdlibModulesFromLibPath:PythonScripts.RunsGoldenFiles'`
- `ctest --test-dir build --output-on-failure`

## 2026-05-20 Stdlib Foundation Slice

Review focus:

- Put `inspect`, `gc`, and `collections.namedtuple` behind a real stdlib load
  path instead of ad hoc builtin hooks.
- Expose code/frame metadata through typed descriptors so script-level
  introspection can stay clean.
- Make GC a real mark-and-sweep pass that treats active frames and roots as
  first-class reachability sources.

Completed:

- Added `Lib/` to the default module search path.
- Added early runtime private stdlib module loading for:
  - `_inspect`
  - `_gc`
  - `_collections`
  - `_sys`
  - `_os`
  This inventory was later corrected: `sys`, `gc`, and `posix` are CPython
  builtin module names, not local underscore hook names.
- Added script-level public wrappers for:
  - `inspect`
  - `gc`
  - `collections`
  - `sys`
  - `os`
- Added `inspect.currentframe`, `isframe`, `iscode`, `isfunction`,
  `getsourcefile`, and `getfile`.
- Added `gc.collect`, `get_count`, `get_objects`, `isenabled`, `enable`, and
  `disable` with real heap marking and sweep.
- Added `collections.namedtuple` with tuple-backed named field access and
  `_fields` / `__match_args__`.
- Added minimal `sys.argv`, `sys.path`, `sys.modules`, `sys.builtin_module_names`,
  and `sys.implementation` plumbing.
- Added minimal `os.name`, `os.sep`, `os.linesep`, `os.curdir`, and
  `os.pardir` plumbing.
- Added code/frame descriptors for:
  - `co_name`, `co_filename`, `co_argcount`, `co_flags`, `co_stacksize`
  - `co_consts`, `co_names`, `co_varnames`, `co_cellvars`, `co_freevars`
  - `co_firstlineno`
  - `f_code`, `f_locals`, `f_globals`, `f_builtins`, `f_lasti`, `f_lineno`
- Added Python golden coverage for stdlib module surfaces.
- Added C++ import coverage for stdlib modules.

Known debt and next risk:

- `gc` is functional but still minimal; generation tracking, thresholds, and
  finalizer policy are not exposed yet.
- `inspect` has a narrow API slice; traceback/frame chain objects are still the
  next diagnostic layer.
- `collections.namedtuple` is useful but still a small subset of CPython's
  richer helper/module surface.
- `sys` and `os` are still minimal and should grow through script modules
  first, with C++ only providing private data hooks where necessary.

Verification:

- pending

## 2026-05-20 Loop Control + F-String + CodeObject Stacksize Slice

Review focus:

- Close the control-flow gap for `break` / `continue` / `for ... else` so loop
  semantics are available in parser/compiler/VM together.
- Expand f-string support beyond the foundation slice with real conversion and
  format-spec pathways while keeping syntax errors explicit for unsupported
  branches.
- Add `CodeObject` stack metadata with control-flow-aware validation instead of
  linear stack-effect accumulation.

Completed:

- Added parser support for `break`, `continue`, and `for ... else`, including
  syntax errors for loop-control statements outside loops.
- Extended AST/symbol passes for new loop statements and `for` else-bodies.
- Added dedicated bytecode opcodes `BREAK_LOOP` and `CONTINUE_LOOP`.
- Routed loop-control pending state through VM `finally` unwinding so
  `continue` and `break` both preserve Python control-flow behavior through
  `try/finally`.
- Expanded f-string parsing/codegen to support:
  - conversion flags `!r` and `!s`
  - `:{spec}` syntax forwarding through runtime `format(...)`
  - explicit syntax rejection for unsupported conversion `!a`
- Added `CodeObject::stack_size()` and control-flow-state stack validation
  during code object construction.
- Added/updated C++ and Python tests:
  - compile errors for out-of-loop `break` / `continue`
  - runtime golden tests for `for ... else` and loop control through `finally`
  - f-string conversion/spec success coverage
  - opcode metadata coverage for new loop-control opcodes
  - `CodeObject` maximum stack-size metadata coverage

Known debt and next risk:

- f-string `!a` and nested replacement fields inside format specs are still not
  implemented.
- Loop-control semantics are now correct through `finally`, but deeper parity
  work is still needed for the full control-flow family (`while`, `async for`,
  richer unwind diagnostics).
- `inspect`, `gc`, and `collections.namedtuple` module surfaces are still
  pending and remain the next user-visible stdlib compatibility gap.
- Stack metadata is now CFG-aware, but this is still a verifier-light model; a
  fuller bytecode validator (block stack discipline and exception edges) is
  still valuable before exposing code-object mutation APIs.

Verification:

- `cmake --build build -j2`
- `build/test_core_objects --gtest_filter='PythonScripts.RunsGoldenFiles'`
- `ctest --test-dir build --output-on-failure`

## 2026-05-19 From-Import Statement Slice

Review focus:

- Extend import syntax without bypassing the existing runtime import/cache
  substrate.
- Keep from-import lowering explicit in compiler bytecode so symbol analysis
  and scope behavior stay consistent.
- Reject dotted module paths for now to avoid pretending package semantics
  before importer architecture is widened.

Completed:

- Added `from` token support.
- Added `FromImportStmt` AST and parser lowering for:
  - `from module import name`
  - `from module import name as alias`
  - comma-separated imported names
- Lowered `from` imports through existing `__import__` then attribute loads.
- Integrated from-import targets with symbol collection and function-local
  analysis.
- Added Python golden coverage for module-level and function-local from-import.
- Added syntax coverage for unsupported dotted module paths.

Known debt and next risk:

- `from pkg import submod` package semantics are not complete until package
  importer support exists (`__path__`, `__spec__`, and top-level import return
  behavior alignment).
- `from module import *` is intentionally unsupported and should be added only
  with symbol-table and module export policy work.
- Current from-import error mapping still reflects attribute lookup behavior;
  CPython-level `ImportError` normalization can be added in runtime import APIs.

Verification:

- `cmake --build build -j2`
- `build/test_core_objects --gtest_filter='Compile.RaisesSyntaxErrorForUnsupportedDottedFromImport:PythonScripts.RunsGoldenFiles:Compile.CompilesAndExecutesBasicFString'`
- `ctest --test-dir build --output-on-failure`

## 2026-05-19 F-String Foundation Slice

Review focus:

- Land f-string support as real compiler/VM infrastructure instead of reusing
  plain string tokens or ad hoc runtime concatenation.
- Keep phase boundaries clean: lexer tokenization, parser part splitting,
  compiler lowering, VM formatting.
- Support the common base case first while rejecting unsupported conversion and
  format-spec syntax with explicit errors.

Completed:

- Added `FString` token kind and `FStringExpr` AST with mixed literal and
  expression parts.
- Added parser decomposition for `{expr}` parts, escaped braces (`{{`/`}}`),
  and nested delimiter tracking inside f-string expressions.
- Added `FORMAT_VALUE` opcode and VM dispatch using `__format__("")`, with
  type validation that `__format__` returns `str`.
- Lowered f-strings to `FORMAT_VALUE` + string concatenation in bytecode.
- Added C++ coverage for bytecode metadata, successful f-string execution, and
  syntax rejection for unsupported `!`/`:` conversion-spec features.
- Added Python golden coverage for basic f-string output and conversion-spec
  syntax errors.

Known debt and next risk:

- Conversion flags (`!r`, `!s`, `!a`) and format specs (`:{...}`) are still
  rejected and should be added on top of the same `FStringExpr` substrate.
- Nested f-strings in format specs remain unsupported.
- Error locations for inner f-string expressions currently point to the
  f-string token column, not exact inner offsets.

Verification:

- `cmake --build build -j2`
- `build/test_core_objects --gtest_filter='Compile.CompilesAndExecutesBasicFString:Compile.RaisesSyntaxErrorForUnsupportedFStringConversion:Bytecode.MetadataNamesAndStackEffects:PythonScripts.RunsGoldenFiles'`
- `ctest --test-dir build --output-on-failure`

## 2026-05-19 Traceback Chain Seed Slice

Review focus:

- Move beyond a single source-location seed so nested function failures are
  diagnosable from the CLI.
- Keep traceback data attached to the Python exception object, not to CLI-only
  formatting state.
- Preserve the existing first-error source accessors while adding a growable
  frame chain for future traceback objects.

Completed:

- Added `ExceptionObject::TracebackEntry` storage and accessors.
- Changed VM exception annotation to append frame entries as exceptions
  propagate across frames.
- Kept `source_filename/source_line/source_name` as the innermost frame for
  existing callers and tests.
- Updated CLI error rendering to print the traceback chain in
  "most recent call last" order.
- Added C++ coverage for single-frame and nested-frame traceback entries.
- Added CLI coverage for nested Python tracebacks and updated Python golden
  output for function-scope errors.

Known debt and next risk:

- Traceback entries are still structured C++ data on `ExceptionObject`, not a
  script-visible `traceback` object with `tb_next`, `tb_frame`, and `tb_lineno`.
- Exception chaining (`__cause__`, `__context__`, `__suppress_context__`) is
  still absent.
- `Frame` objects are script-visible seeds, but traceback frames should become
  ordinary GC-managed objects before debugger/sys APIs expand.

Verification:

- `cmake --build build -j2`
- `build/test_core_objects --gtest_filter='Compile.EmitsLineTableAndAttachesRuntimeErrorLocation:Compile.KeepsInnerFunctionRuntimeErrorLocation:Cli.ReportsNestedPythonTracebacks:PythonScripts.RunsGoldenFiles'`

## 2026-05-19 VM Depth Guard Slice

Review focus:

- Stop unbounded VM recursion before recursive Python calls can consume host
  stack unpredictably.
- Add a value-stack guard for malformed bytecode and future compiler bugs.
- Use Python-visible `RecursionError` rather than reporting this through
  generic C++ bytecode failures.

Completed:

- Added `RecursionError` to the exception hierarchy, runtime accessors,
  factories, helper raiser, roots, and builtins.
- Added `ThreadState::MaxFrameDepth` and enforced it in `push_frame`.
- Added `Frame::MaxValueStackDepth` and enforced it in `Frame::push`.
- Added C++ coverage for exception hierarchy, builtins exposure, frame-depth
  overflow, and value-stack overflow.
- Updated Python builtins golden output to include `RecursionError`.

Known debt and next risk:

- The recursion limit is currently a fixed VM constant. A script-visible
  `sys.getrecursionlimit()` / `sys.setrecursionlimit()` belongs with the first
  `sys` module slice.
- `CodeObject` still lacks a computed/validated `stacksize`; value-stack
  pressure is guarded dynamically but not proven at compile time.
- Full traceback objects remain the next diagnostic substrate needed for deep
  recursive failures.

Verification:

- `cmake --build build -j2`
- `build/test_core_objects --gtest_filter='CoreObjects.RuntimeBootstrapsExceptionHierarchy:Module.BootstrapsBuiltinsModule:Bytecode.EnforcesFrameAndValueStackDepthLimits:PythonScripts.RunsGoldenFiles'`

## 2026-05-19 For Iteration Slice

Review focus:

- Add `for` on top of the object iteration protocol instead of lowering only
  `range()` as a special case.
- Keep loop execution in VM bytecode so future `break`, `continue`, `else`,
  and exception unwinding have a single control-flow substrate.
- Preserve function-scope local analysis for loop targets.

Completed:

- Added `for` tokenization and AST representation.
- Added parser support for `for name in iterable:` blocks.
- Added `GET_ITER` and `FOR_ITER` bytecode metadata and stack effects.
- Added VM dispatch through `object_iter` / `object_next`, with
  `StopIteration` consumed only by `FOR_ITER`.
- Lowered loops through normal binding stores, so globals, locals, cells, and
  future nonlocal loop targets share the existing name machinery.
- Added Python golden coverage for `range`, string, and list iteration,
  mutation inside a loop, and function-local loop variables.

Known debt and next risk:

- Loop targets are currently simple names; tuple unpacking, attribute targets,
  and subscript targets should extend the assignment-target AST rather than
  adding ad hoc parser branches.
- `break`, `continue`, and `for ... else` still need block-stack integration.
- Iterator protocol is now on the VM path, so traceback and stack/depth guards
  become more important before recursive/user-defined iterators grow.

Verification:

- `cmake --build build -j2`
- `build/test_core_objects --gtest_filter='Bytecode.MetadataNamesAndStackEffects:PythonScripts.RunsGoldenFiles'`

## 2026-05-19 Import Statement Slice

Review focus:

- Add `import` syntax without bypassing the existing runtime module registry,
  cache, search-path, and failure-cleanup substrate.
- Keep unsupported package semantics explicit until `__package__`, `__path__`,
  `__spec__`, and top-level package return rules are designed.
- Make function-scope imports participate in symbol analysis as real local
  bindings.

Completed:

- Added `import` tokenization and AST representation for import aliases.
- Added parser support for `import name` and `import name as alias`, including
  duplicate binding checks.
- Lowered import statements through the existing `__import__` callable and
  normal name-binding path.
- Updated symbol collection and function local analysis so import aliases bind
  correctly at module and function scope.
- Added Python golden coverage for module import, alias import, cache identity,
  and import inside a function.

Known debt and next risk:

- Dotted and relative imports remain rejected at parse/import-runtime
  boundaries; package objects need a separate coherent slice.
- `from ... import ...` is not implemented yet and should share the same future
  package/import request model rather than duplicating parser-specific logic.
- Import still uses the builtin `__import__` surface directly during lowering;
  this is acceptable for the current substrate, but full importlib alignment
  will need a typed import opcode or runtime import request object.

Verification:

- `cmake --build build -j2`
- `build/test_core_objects --gtest_filter='PythonScripts.RunsGoldenFiles:Module.ImportsPythonModulesThroughRuntimeRegistry'`

## 2026-05-19 Lexer String And Comment Slice

Review focus:

- Remove the single-quote-only lexer debt before adding more Python golden
  tests.
- Keep comments and blank/comment-only lines from disturbing indentation
  semantics.
- Make unsupported f-strings fail at the lexical boundary with a clear
  `SyntaxError` instead of being parsed as accidental adjacent tokens.

Completed:

- Added double-quoted string literals on the same string token path as
  single-quoted literals.
- Added triple-quoted single and double string literals with multiline line
  tracking.
- Added `#` trailing and full-line comments.
- Allowed blank/comment-only lines between a compound statement header and the
  first indented statement.
- Added an explicit f-string unsupported diagnostic to reserve the future AST
  path for `JoinedStr`/`FormattedValue` rather than adding lexer shortcuts.
- Added Python golden coverage for double quotes, mixed quotes, escapes,
  triple-quoted strings, trailing comments, and comments inside blocks.
- Added C++ compile coverage for the f-string diagnostic.

Known debt and next risk:

- Real f-strings still need a parser/compiler design: `JoinedStr` and
  `FormattedValue` AST nodes, expression parsing inside braces, conversion
  flags, format specs, and lowering through `format()`/string concatenation.
- The lexer still intentionally supports only the currently modeled escape
  subset; raw/bytes prefixes should land together with bytes and buffer types.
- `import` statements, `for` lowering, traceback objects, and VM stack/depth
  guards remain the next high-value infrastructure work.

Verification:

- `cmake --build build -j2`
- `build/test_core_objects --gtest_filter='Compile.RaisesSyntaxErrorForUnsupportedFString:PythonScripts.RunsGoldenFiles'`

## 2026-05-19 Slice Substrate Slice

Review focus:

- Add slicing as a normal object-space substrate, not as parser-only list
  special casing.
- Keep slice syntax lowering, VM dispatch, builtin `slice`, and sequence
  `__getitem__` on one coherent path.
- Cover negative-step defaults because that is where slice implementations
  most often drift from Python semantics.

Completed:

- Added `SliceObject`, `slice` builtin type, runtime factory/accessor/rooting,
  repr, traversal, and constructor semantics.
- Added `slice.indices(length)` with CPython-style default adjustment for
  positive and negative steps.
- Added `BUILD_SLICE` bytecode and VM dispatch.
- Added parser/AST/compiler support for `a[start:stop:step]`, including
  omitted start/stop/step components.
- Added list, tuple, and str slicing through the existing `getitem` slot path.
- Added Python golden coverage for `slice()`, `indices()`, list/tuple/str
  slicing, and reverse slicing.

Known debt and next risk:

- Slice assignment and deletion remain intentionally deferred until list
  mutation semantics are widened beyond single-index operations.
- Range slicing should return a range object later; currently this slice
  substrate is wired for list, tuple, and str.
- Slice components currently accept `int` or `None`; future `__index__`
  support should be centralized across indexing and slicing.

Verification:

- `cmake --build build -j2`
- `build/test_core_objects --gtest_filter='PythonScripts.RunsGoldenFiles:Bytecode.MetadataNamesAndStackEffects:Module.BootstrapsBuiltinsModule'`
- `ctest --test-dir build --output-on-failure`

## 2026-05-19 Symbol Analysis Module Slice

Review focus:

- Pay down the compiler structure debt left by closure, global/nonlocal, and
  class `__class__` work.
- Keep symbol analysis as a first-class compiler phase instead of burying it
  inside bytecode emission.
- Preserve existing scope semantics exactly while preparing for default
  arguments, comprehensions, and richer class scope rules.

Completed:

- Extracted `SymbolTable`, function/class symbol analysis, lexical reference
  collection, directive collection, and module/eval symbol collection into
  `Python/compiler_symbols.cpp` / `Python/compiler_symbols.hpp`.
- Kept `Python/compiler.cpp` focused on bytecode emission and public compile
  entrypoints.
- Moved class-body `__class__` cell analysis behind the same symbol module as
  normal function closure analysis.
- Removed the old in-file symbol-analysis implementation rather than leaving
  a disabled duplicate.

Known debt and next risk:

- Declaration ordering diagnostics such as "used prior to global declaration"
  remain incomplete and now belong naturally in `compiler_symbols.cpp`.
- `compiler_symbols.cpp` is intentionally phase-local but still duplicates
  visitor traversal patterns between name collection and lexical-reference
  collection. That should be simplified once comprehensions/default arguments
  define the next scope surface.
- The next bottom substrate slice can proceed to `slice` and sequence slicing
  with less risk of growing `compiler.cpp` into another monolith.

Verification:

- `cmake --build build -j2`
- `build/test_core_objects --gtest_filter='Compile.*:Module.*:Cli.*:PythonScripts.RunsGoldenFiles'`

## 2026-05-19 Closure Cell And Super Slice

Review focus:

- Turn zero-argument `super()` into real object-space behavior on top of the
  shared cell/freevar substrate.
- Keep `__class__` rooted in class-body closure analysis instead of a VM
  special case.
- Avoid duplicating class-cell policy across compiler and runtime layers.

Completed:

- Added a runtime `super` type backed by `Objects/object_super.cpp`.
- Exposed `super` through builtins and runtime bootstrap.
- Lowered zero-argument `super()` in methods by marking an implicit
  `__class__` free variable.
- Emitted class-body `__class__` cell metadata and backfilled `__classcell__`
  after class construction.
- Kept two-argument `super(type, obj)` on the same descriptor and MRO path.
- Added Python golden coverage for zero-arg and explicit-arg `super()`.

Known debt and next risk:

- `super()` still depends on active frame metadata, so future class/method
  transformations must preserve the current call frame contract.
- Class-body closure support is now correct for `super()` and direct
  `__class__` use in methods, but the broader class protocol still needs more
  descriptor and metaclass work before it feels complete.
- The next structural cleanup should move the symbol-analysis machinery out
  of `compiler.cpp` before more scope rules and class semantics land.

Verification:

- `cmake --build build -j2`
- `ctest --test-dir build --output-on-failure`

## 2026-05-19 Global And Nonlocal Symbol Slice

Review focus:

- Tighten the symbol-analysis debt left after closure cells.
- Avoid treating `global` as a runtime locals hack; function global writes need
  explicit global opcodes.
- Keep `nonlocal` on the same closure-cell path as ordinary free variables.

Completed:

- Added lexer/parser/AST support for `global` and `nonlocal` statements.
- Added `LOAD_GLOBAL`, `STORE_GLOBAL`, and `DELETE_GLOBAL` bytecode.
- Extended VM global opcodes to read globals/builtins and mutate globals.
- Added function-scope directive collection before local analysis so global
  and nonlocal assignment targets do not become fast locals.
- Validated `nonlocal` bindings against enclosing lexical scopes.
- Lowered `nonlocal` reads/writes through existing deref/cell machinery.
- Added C++ and Python golden coverage for global mutation, nonlocal mutation,
  freevar pass-through with `nonlocal`, and invalid missing nonlocal bindings.

Known debt and next risk:

- CPython's ordering diagnostics such as "used prior to global declaration"
  are not enforced yet. The current implementation applies declarations to
  the whole function block, which is correct for successful programs but still
  too permissive for malformed source.
- Class-body directive semantics still need a deliberate design when class
  `__class__` cells are added.
- Symbol analysis is now large enough that the next structural cleanup should
  move it out of `compiler.cpp` before default arguments, comprehensions, or
  class closure semantics expand it further.

Verification:

- `cmake --build build -j2`
- `build/test_core_objects --gtest_filter='Bytecode.MetadataNamesAndStackEffects:Compile.CompilesGlobalAndNonlocalDeclarations:Compile.RejectsInvalidNonlocalDeclaration:PythonScripts.RunsGoldenFiles'`
- `ctest --test-dir build --output-on-failure`

## 2026-05-19 Closure Cell Foundation Slice

Review focus:

- Build closures as real object-space cells, not copied values or globals
  fallback.
- Keep frame construction GC-ready by allocating cells before the `Frame`
  object is constructed.
- Make compiler, CodeObject, function object, and VM agree on one
  `cellvars/freevars` ordering before `super()` and `__class__` cells build on
  it.

Completed:

- Added `CellObject` with repr, traversal, runtime type bootstrap, and
  `Runtime::new_cell`.
- Extended `CodeObject` with `cellvars` and `freevars` metadata.
- Added `LOAD_DEREF`, `STORE_DEREF`, `DELETE_DEREF`, and `LOAD_CLOSURE`.
- Added closure vectors to `PythonFunctionObject`, function traversal, and
  function execution requests.
- Moved frame cell creation into `Runtime::new_frame`, with temporary roots
  for newly allocated cells and a constructor that only validates final frame
  state.
- Extended compiler symbol analysis to identify function cell/free variables
  and pass free variables through intermediate nested functions.
- Lowered nested function creation to load closure cells, build a closure
  tuple, and call `MAKE_FUNCTION` with closure metadata.
- Added C++ and Python golden coverage for shared closure cells, mutation after
  inner function creation, parameter capture, and freevar pass-through.

Known debt and next risk:

- Class-body closure semantics remain incomplete: no `__class__` cell,
  `LOAD_CLASSDEREF`, or zero-argument `super()` yet.
- Empty-cell error messages are intentionally minimal and need CPython-style
  local/free-variable wording once traceback objects are richer.
- Symbol analysis is still local to `compiler.cpp`; as function/class scope
  semantics grow, it should become a dedicated module with explicit symbol
  states rather than visitor plumbing embedded beside bytecode emission.

Verification:

- `cmake --build build -j2`
- `build/test_core_objects --gtest_filter='Bytecode.MetadataNamesAndStackEffects:Bytecode.DispatchesMakeFunctionWithClosure:Compile.CompilesNestedFunctionClosureCells:Compile.PassesFreeVariablesThroughIntermediateFunctions:PythonScripts.RunsGoldenFiles'`
- `ctest --test-dir build --output-on-failure`

## 2026-05-19 Module Registry And Import Substrate Slice

Review focus:

- Expose `__import__` only after runtime-owned module cache, search paths, and
  failed-import cleanup exist.
- Keep import execution on the same compiler/VM/module dictionary path used by
  CLI scripts rather than adding a builtin-side shortcut.
- Avoid pretending package import is supported before package metadata and
  top-level return semantics are designed.

Completed:

- Added `ImportError` to the exception hierarchy and builtins.
- Added runtime-owned `modules` registry and module search path state.
- Registered newly-created modules in the runtime registry, including
  `__main__` and imported modules.
- Added `Runtime::import_module` for simple `.py` module loading:
  - searches explicit runtime paths
  - inserts the module in the cache before execution
  - executes with module globals, runtime builtins, and `ExecutionRequest`
  - returns cached modules on repeated import
  - removes failed imports from the registry
- Added `builtins.__import__` as a thin wrapper over the runtime import API.
- CLI script execution now adds the script directory to the import search path;
  REPL adds the current working directory.
- Added C++ coverage for cache reuse, failure cleanup, missing module
  `ImportError`, and CLI sibling-module import.
- Added Python golden coverage for direct `__import__` and cache identity.

Known debt and next risk:

- This slice intentionally supports only simple module names. Dotted package
  imports remain blocked until package objects, `__package__`, `__path__`,
  `__spec__`, and CPython-compatible top-level return semantics exist.
- There is still no `import` statement parser/lowering; Python code must call
  `__import__` directly for now.
- Search paths are runtime-owned but there is no script-visible `sys.path` or
  `sys.modules` module yet.
- Import execution has no module lock or per-interpreter import stack beyond
  early cache insertion. Circular simple imports can observe partial modules,
  but package-level diagnostics need a richer import state object.

Verification:

- `cmake --build build -j2`
- `build/test_core_objects --gtest_filter='Module.*:Cli.ImportsSiblingPythonModule:PythonScripts.RunsGoldenFiles'`
- `ctest --test-dir build --output-on-failure`

## 2026-05-19 Class Construction Substrate Slice

Review focus:

- Remove the VM-owned class construction policy before metaclass, import, and
  descriptor work grow on top of it.
- Make class statements use a Python-visible `__build_class__` path rather
  than the old direct `MAKE_CLASS` lowering.
- Keep `Runtime::new_class` as the low-level type allocator used by
  `type(...)` / `type.__new__`.

Completed:

- Added `ClassBuildRequest` and `Runtime::build_class`.
- Added `builtins.__build_class__` for Python function bodies.
- Lowered class statements to load `__build_class__`, create a class-body
  function, and call it with the class name, evaluated bases, and class
  keywords.
- Added parser/AST/compiler support for class keyword arguments, including
  `metaclass=`.
- Moved class-body namespace preparation, metaclass selection,
  `__prepare__`, class-body execution, and final metaclass call behind the
  runtime class-build API.
- Kept `MAKE_CLASS` as a thin legacy bytecode dispatch that delegates to the
  same runtime API.
- Added Python golden coverage for `metaclass=`, `__prepare__` ordering, and
  direct `__build_class__` use.

Known debt and next risk:

- `__prepare__` currently must return a `dict`. Full Python semantics require
  accepting mapping objects once the mapping protocol can back `STORE_NAME`.
- Class keywords are validated instead of silently ignored, but only
  `metaclass=` is supported. Arbitrary class keywords should be forwarded to
  custom metaclasses after `type.__new__` / `type.__init__` keyword handling is
  designed.
- Non-type callable metaclasses are intentionally rejected until class result
  validation, call protocol, and layout guarantees are ready for them.
- Class body lowering now uses a function object, but `__class__` cells,
  zero-argument `super()`, and closure-aware class scopes remain in the
  closure/cell substrate slice.
- `MAKE_CLASS` remains only for bytecode compatibility tests. New compiler
  output should continue through `__build_class__`.

Verification:

- `cmake --build build -j2`
- `build/test_core_objects --gtest_filter='Bytecode.DispatchesMakeClass:Bytecode.MakeClassUsesEvaluatedBases:Bytecode.RejectsMalformedBytecode:Compile.*Class*:PythonScripts.RunsGoldenFiles'`
- `ctest --test-dir build --output-on-failure`

## 2026-05-19 Execution Request Substrate Slice

Review focus:

- Reduce the direct VM execution-call debt identified in the substrate plan.
- Keep old public VM overloads for tests and host convenience, but make them
  delegate to one explicit execution request path.

Completed:

- Added `ExecutionMode` and `ExecutionRequest`.
- Added `VirtualMachine::execute(const ExecutionRequest &)`.
- Routed existing VM execute overloads through `ExecutionRequest`.
- Routed function calls, `eval`, `exec`, REPL execution, script execution, and
  class-body execution through the new request structure.
- Added focused C++ coverage that verifies explicit locals/globals/builtins
  namespaces travel through `ExecutionRequest`.

Known debt and next risk:

- `ExecutionRequest::module` and `ExecutionMode` are now explicit but mostly
  advisory. The next slices should use them for builtins injection,
  `__main__` registration, import execution, class-body semantics, and
  traceback display names.
- `MAKE_CLASS` still owns policy. With execution requests in place, the next
  natural code slice is `__build_class__` and metaclass-aware class body
  execution.

Verification:

- `cmake --build build -j2`
- `build/test_core_objects --gtest_filter='Bytecode.ExecutionRequestCarriesExplicitNamespaces:Functions.PythonFunctionCallsReuseActiveVirtualMachineFrames:PythonScripts.RunsGoldenFiles:Cli.ExecutesScriptFile:Cli.RunsInteractiveReplFromStandardInput'`
- `ctest --test-dir build --output-on-failure`

## 2026-05-19 Bottom Substrate Planning Slice

Review focus:

- Avoid implementing `__build_class__`, `__import__`, `super`, `slice`,
  bytes/buffer, and IO as isolated stubs.
- Identify the shared substrate needed before those names become stable
  Python-visible APIs.
- Resolve whether any immediate technical debt blocks the next implementation
  slice.

Completed:

- Added `doc/bottom_substrate_design.md`.
- Planned the dependency order:
  - execution request substrate
  - class construction and `__build_class__`
  - module registry and import
  - closure/cell support for `super`
  - slice object and sequence slicing
  - bytes/bytearray/memoryview and buffer protocol
  - IO substrate for `open` and `input`
  - traceback and exception-state objects
- Updated the documentation index.

Technical-debt review:

- `compile` / `eval` / `exec` are currently correct enough as a first slice
  because they reuse compiler, CodeObject, active frame, and VM execution.
  The debt is structural: CLI, builtins, function calls, import, and class-body
  execution still call VM entrypoints directly. The next implementation slice
  should extract a reusable execution request helper before import or
  `__build_class__` grow around the old direct calls.
- `MAKE_CLASS` remains the highest class-system debt. It still owns class-body
  execution and namespace preparation in the VM. The next feature slice should
  move class statement semantics behind a `__build_class__`-compatible runtime
  API.
- No code change was made in this planning slice because the immediate risk is
  interface design churn, not a localized bug. The next slice should be code,
  beginning with execution request extraction.

Verification:

- Documentation-only slice; no runtime behavior changed.

## 2026-05-19 Builtin Iterator And Dynamic Execution Slice

Review focus:

- The builtin expansion must grow from object-space substrate: real types,
  slots, constructors, traversal, and Python golden semantics.
- Iterator builtins should not collect eagerly. They must compose through
  `object_iter` / `object_next` so future `for` lowering can reuse exactly the
  same path.
- Dynamic execution builtins should reuse compiler and VM entry points rather
  than evaluating strings through ad hoc host-side shortcuts.

Completed:

- Added real runtime types for:
  - `range`
  - `range_iterator`
  - `enumerate`
  - `zip`
  - `map`
  - `filter`
- Exposed `range`, `enumerate`, `zip`, `map`, and `filter` from the builtins
  module as type objects.
- `range` now supports repr, len, getitem, containment, and iteration.
- `enumerate`, `zip`, `map`, and `filter` now implement lazy
  `__iter__` / `__next__` behavior through slots.
- Added `format()` through normal `__format__` attribute dispatch.
- Added minimal `compile()`, `eval()`, and `exec()` builtins wired to the
  existing compiler, CodeObject, active frame, and VM execution paths.
- Expanded `Tests/python/builtins.py` to pin range, iterator-family, format,
  compile, eval, and exec behavior.

Known debt and next risk:

- `compile` currently supports only `exec` and `eval` modes. It lacks flags,
  dont_inherit, optimize, AST input, and richer diagnostics.
- `eval` / `exec` accept dict globals/locals only and rely on the current live
  frame dictionaries. Optimized locals, closure cells, and builtins injection
  rules still need CPython-compatible design.
- `range` length arithmetic is intentionally compact and does not yet defend
  every int64 overflow edge. A future arbitrary-precision `int` layer should
  own exact range math.
- `map` / `filter` / `zip` / `enumerate` are lazy object-space iterators, but
  they still lack CPython-level repr strings and constructor error wording.
- `__build_class__` should be next for class-system openness: class statements
  still go through the VM `MAKE_CLASS` path rather than a visible builtin.
- `__import__` needs module cache/search-path state before it can be more than
  a stub.
- `bytes`, `bytearray`, and `memoryview` need a byte/buffer substrate.
- `open` and `input` need a runtime-managed IO layer.
- `slice` should be designed together with slice syntax and sequence slicing.
- `super` needs compiler support for class/function context cells.
- `help` should wait for docstrings/module metadata and output policy.

Verification:

- `cmake --build build -j2`
- `build/cxxpython Tests/python/builtins.py`
- `build/test_core_objects --gtest_filter='PythonScripts.RunsGoldenFiles'`

## 2026-05-19 Try Finally And Comparison Foundation Slice

Review focus:

- `try/finally` must be VM control-flow infrastructure, not a compiler-only
  jump trick. It has to preserve pending returns, propagate exceptions after
  cleanup, and let a finally-raised exception replace the earlier pending
  control flow.
- `is` and `in` are language-level comparisons, not builtins. `is` belongs to
  object identity and `in` belongs to the object containment protocol.
- The requested builtin list should be staged by missing substrate instead of
  faking surface names.

Completed:

- Added `finally` token/parser support and extended `TryStmt` with
  `finally_body`.
- Added VM block handlers for nearest-block unwinding across `except` and
  `finally`.
- Added `SETUP_FINALLY`, `POP_FINALLY`, and `END_FINALLY`.
- `return` now records pending return state and runs active finally handlers
  before returning.
- Unhandled exceptions now run active finally handlers before propagation.
- A new exception raised inside finally overrides the pending return/exception.
- Added language-level `is`, `is not`, `in`, and `not in` comparison support.
  Membership dispatches through `object_contains`.
- Added C++ and Python golden coverage for normal finally, exceptional
  finally, try/except/finally ordering, return-through-finally, and finally
  exception override.

Builtins/substrate analysis:

- Ready for near-term implementation on existing substrate:
  - `range`: needs a compact `range` object, integer argument parsing, length,
    getitem, containment, iteration.
  - `map`, `filter`, `zip`, `enumerate`: need iterator object families and
    StopIteration-safe composition over `object_iter` / `object_next`.
  - `format`: can start from `object.__format__`, then grow through type slots.
  - `__import__`: can start as a bootstrap module loader once module search
    path and module cache are explicit runtime state.
  - `__build_class__`: should replace the VM-only class construction path so
    class statements become script-visible and metaclass-aware.
- Requires parser/compiler/runtime expansion first:
  - `compile`, `eval`, `exec`: need mode-aware parser entry objects, code
    object flags, explicit globals/locals argument handling, and safer source
    diagnostics.
  - `super`: needs full descriptor/MRO binding semantics and class/function
    context cells.
  - `slice`: useful only after slice syntax and sequence slicing are designed.
- Requires byte/buffer/io substrate first:
  - `bytes`, `bytearray`, `memoryview`: require byte storage types, buffer
    protocol, integer-byte validation, and repr/sequence semantics.
  - `open`, `input`: require an IO layer, text encoding decisions, error
    handling, and runtime-managed stdin/stdout/stderr.
- Introspection/help:
  - `dir` exists but remains a simplified merge. `help` should wait for
    docstrings, module metadata, and paging/output policy.

Known debt and next risk:

- `finally` now handles normal, return, and exception control flow. It does
  not yet cover `break`/`continue` because those statements do not exist yet.
- Bare `raise`, exception chaining, traceback objects, `except (...)`, and
  `try/else` remain open.
- The new comparison opcode operand is now `ComparisonOp`, while rich compare
  still owns the first six values. Keep that invariant documented if new
  comparison forms are added.

Verification:

- `cmake --build build -j2`
- `build/test_core_objects --gtest_filter='Bytecode.MetadataNamesAndStackEffects:Compile.CompilesIdentityAndMembershipComparisons:Compile.CompilesAndExecutesTryFinallyControlFlow:Compile.TryExceptFinallyUsesNearestBlockDuringUnwind:PythonScripts.RunsGoldenFiles'`

## 2026-05-19 Foundation Str Sequence Slice

Review focus:

- The base object protocol should not stop at `int`; `str` now needs to behave
  like a real sequence before higher syntax grows on top of it.
- Reflection and protocol coverage must be backed by real slots, not just
  names in `dir()`.

Completed:

- `str` now exposes sequence slots for `__len__`, `__getitem__`,
  `__contains__`, and `__iter__`.
- `dir(str)` and `dir('abc')` now surface the corresponding slot wrappers.
- Python-level coverage now fixes `len('abc')`, string indexing,
  `.__contains__`, and `list('abc')`.
- Added a focused C++ test that exercises the string sequence protocol
  through the shared object protocol APIs.

Known debt and next risk:

- `in` / `not in` syntax is still not lowered by the parser, so membership is
  currently exercised through `__contains__` and `list(iterable)` instead of
  expression syntax. That is the next natural parser/VM slice, not a builtin
  shortcut.
- `str` is still only a basic sequence slice. Repetition, slicing, and a wider
  string method table remain open.

Verification:

- `cmake --build build -j2`
- `build/test_core_objects --gtest_filter='CoreObjects.StringProvidesSequenceProtocol:PythonScripts.RunsGoldenFiles'`
- `build/cxxpython Tests/python/builtins.py`

## 2026-05-19 Init, Exception Syntax, REPL, And Builtins Slice

Review focus:

- Closed the current `__init__` construction gap before adding more class
  surface area.
- Audited the exception path as a VM/block-stack feature, not a CLI or builtin
  formatting shortcut.
- Continued builtin work only through ordinary module entries and
  `LOAD_NAME`/`CALL`, preserving the object protocol boundary.

Completed:

- Python heap classes now call Python-defined `__init__` through descriptor
  binding during `type.__call__`. A non-`None` return raises `TypeError`.
- Added parser/compiler/bytecode/VM support for:
  - `raise expr`
  - `try` / `except Type`
  - `except Type as name`
  - bare `except`
  - unmatched handler re-raise
- Added VM exception handler stack opcodes:
  - `SETUP_EXCEPT`
  - `POP_EXCEPT`
  - `CHECK_EXCEPTION_MATCH`
  - `RAISE_VALUE`
- Exception types are constructible through normal type calls, so
  `raise TypeError('boom')` creates an `ExceptionObject` with args/message
  semantics instead of relying on host-side strings.
- Replaced the no-argument CLI placeholder with a minimal REPL:
  expression input compiles through `compile_eval_input` and prints `repr`;
  statements and compound blocks compile through `compile_exec_input` and run
  in a persistent `__main__` module.
- Upgraded the REPL to use GNU readline/history when attached to a TTY while
  keeping the non-interactive pipe path stable for tests and scripts.
- Expanded builtin coverage beyond the bootstrap core:
  - `all`
  - `any`
  - `sum`
  - `min`
  - `max`
  - `sorted`
  - `dir`
  - `vars`
- Expanded native builtins while keeping them ordinary objects in the
  `builtins` module:
  - `print(..., sep=..., end=...)`
  - `isinstance`
  - `issubclass`
  - `callable`
  - `id`
  - `globals`
  - `locals`
- Added Python golden coverage for class `__init__`, exception syntax,
  builtin introspection, builtin enumeration, and REPL behavior.
- Added C++ coverage for opcode metadata, VM exception dispatch, compiler
  lowering, `__init__` return checking, and unmatched exception propagation.

Known debt and next risk:

- Bare `raise`, `finally`, `else`, tuple exception matching in `except`, and
  exception state (`sys.exc_info`, context/cause, full traceback objects) are
  still open. Do not bolt them onto CLI formatting; they belong in VM exception
  state and runtime traceback objects.
- REPL incomplete-input detection is intentionally minimal and parser-message
  driven. A real interactive parser mode should replace string matching before
  adding more compound statements.
- `dir()` currently reflects a deterministic runtime namespace snapshot rather
  than the full CPython attribute merge algorithm. It is good enough for the
  current builtin/module surface, but future object-space growth may need a
  richer merge of module, instance, and inherited attributes.
- Builtins now cover core introspection, but the native argument parser still
  lacks a structured signature table, optional keyword defaults, positional-only
  markers, keyword-only arguments, and conversion helpers for C/C++ extension
  parity.
- `id()` currently reflects object address identity in the non-moving runtime.
  A moving GC will need stable identity storage or pinning semantics.
- `globals()` and `locals()` return live frame dictionaries, which is correct
  for bootstrap and REPL, but optimized locals and closure cells will need
  CPython-compatible synchronization rules.

Verification:

- `cmake --build build -j2`
- `build/test_core_objects --gtest_filter='Cli.RunsInteractiveReplFromStandardInput:PythonScripts.RunsGoldenFiles'`
- `ctest --test-dir build --output-on-failure`

Result: all 167 tests pass.

## 2026-05-18 Risk Ledger And Immediate Fix Queue

Review scope:

- Audited current AST, slot, type, CodeObject, Frame, Runtime, VM, module, and
  script-test paths before continuing feature work.
- The following risks are active fix items, not background notes. Each stable
  slice should update this ledger before commit.

Eight tracked risks:

1. High, fixed in current slice: multiple inheritance lacked explicit layout
   compatibility checks.
   `Runtime::new_class` inherited slots from the primary base only, so
   arbitrary combinations such as `list` plus `dict` could create a type whose
   C++ object layout could not be allocated or safely operated on.
2. High, core entry fixed in current slice: class creation bypassed metaclass
   selection and `__prepare__`. `MAKE_CLASS`, `type(...)`, and metaclass calls
   now share `ClassConstructionRequest` and runtime metaclass selection;
   syntax-level `metaclass=` and full `__prepare__` invocation remain open.
3. High, first semantics slice fixed in current slice: function scope used to
   be dynamic `LOAD_NAME` lookup. Function locals are now compiler-discovered
   fast locals with `LOAD_FAST`/`STORE_FAST` and `UnboundLocalError`; `global`,
   `nonlocal`, free/cell analysis, and closures remain open.
4. Medium/high, second slice fixed: `CodeObject` now carries execution flags
   for varargs/varkeywords function shape and a per-instruction source line
   table. `Frame` tracks the last executed instruction, VM errors attach
   filename/name/line to exception objects, and CLI script errors print a
   minimal traceback frame. Full traceback objects, exception chaining,
   stacksize, freevars/cellvars, and script-visible debugging metadata remain
   open.
5. Medium, second slice fixed: `Rooted<T>` now provides scoped RAII roots and
   the runtime root list supports nested counted roots. VM dispatch now guards
   popped operands, collection literal elements, call arguments, class
   namespace temporaries, and newly allocated aggregate results across protocol
   calls and allocation-heavy opcode paths. Runtime factories and Python
   function argument binding still need a wider audit before a collecting or
   moving GC is enabled.
6. Medium, partially fixed: builtins remain bootstrap-minimal. Calls now enter
   through shared `CallArguments`, keyword rejection is centralized at the
   callable boundary, `str()` is exposed, `print` uses `object_str`, and
   native/builtin constructor argument count checks now share helper APIs. The
   core iterator protocol now has `iter`/`iternext` slots, `iter()`/`next()`,
   `StopIteration`, sequence and dict-key iterators, and `list(iterable)`.
   `repr()` and `hash()` now dispatch through the same object protocol, and
   core singleton/type names live in the builtins module dict. Core
   constructor types now include basic `int()`, `bool()`, `tuple(iterable)`,
   and `dict()` support through type `new_object`/`init` slots. The current
   CLI/REPL slice adds `print` `sep`/`end`, `isinstance`, `issubclass`,
   `callable`, `id`, `globals`, and `locals`.
   Remaining builtin debt is the richer CPython-style argument parser plus
   protocol users such as `for`, `*iterable`, and `**mapping`.
7. Medium, partially fixed: descriptor and type-attribute protocol is
   incomplete. Lookup is centralized; `property` works as a data descriptor;
   `property.getter`/`setter`/`deleter`, `staticmethod`, and `classmethod` are
   typed builtin descriptors with decorator-script coverage. Attribute delete
   now routes through data descriptors and `property.deleter` at the object API
   layer. `__getattr__` fallback now runs after normal module, metatype, type,
   descriptor, and instance-dict lookup fails. Parser/VM `del` statement
   lowering is now implemented for names, fast locals, attributes, and
   subscripts. `__getattribute__` now wraps the normal lookup path with
   `AttributeError` fallback to `__getattr__`. Slot wrappers now expose
   sequence, mapping, iterator, repr/hash, numeric, and comparison protocol
   dunders as `wrapper_descriptor` / `method-wrapper` objects. Remaining
   descriptor debt is deeper CPython parity around slot replacement from
   Python-defined special methods and member descriptors; `__setattr__` and
   `__delattr__` hooks now run through the public attribute mutation APIs.
8. Medium, first slice fixed: Python functions now support positional
   varargs, keyword dictionaries, and keyword calls through `CodeObject` flags,
   `CALL_KW`, and unified `CallArguments`. Remaining call-family work includes
   `*iterable` / `**mapping` call expansion, keyword-only parameters, default
   arguments, annotations, and a reusable native argument parser.

Immediate order:

- Done in current slice: type layout compatibility now protects later heap
  type, builtin subclass, and C/C++ extension type work.
- Done in current slice: list literal syntax and `BUILD_LIST` now provide
  Python-level list coverage without C++ wrapper setup.
- Done in current slice: class statement, `type(...)`, and metaclass calls now
  share a metaclass-aware construction request.
- Done in current slice: function local assignment names now use fast-local
  semantics and raise `UnboundLocalError` instead of falling through to globals.
- Done in current slice: `CallArguments`, `CALL_KW`, function flags, and
  parser/compiler/runtime support for `def f(a, *args, **kwargs)` plus keyword
  calls.
- Done in current slice: `Rooted<T>` introduces scoped root guards and counted
  root entries, making nested guards safe.
- Done in current slice: `str()` is exposed through the normal type call path
  and `print` now uses `object_str` instead of string-only branching.
- Done in current slice: `property` is now a typed builtin descriptor with
  getter/setter behavior and read-only assignment errors.
- Done in current slice: `CodeObject` line tables, parser statement line
  capture, compiler line emission, VM source-location attachment, and minimal
  CLI traceback output now cover module and nested function runtime errors.
- Done in current slice: `staticmethod` and `classmethod` now live as typed
  descriptor objects, method binding now supports generic callable/self pairs,
  and Python golden tests cover class and instance access.
- Done in current slice: `del` statement lowering now emits VM delete opcodes
  instead of synthetic method calls, and subscript deletion routes through the
  object item protocol.
- Done in current slice: `__getattribute__` is exposed as an object method
  descriptor and user overrides run before normal lookup, with
  `AttributeError` falling through to `__getattr__`.
- Done in current slice: native/builtin positional and keyword validation now
  uses shared helper APIs instead of repeated local string-building branches.
- Done in current slice: VM opcode dispatch now uses scoped root guards around
  popped operands, aggregate construction, function calls, and class creation
  temporaries.
- Done in current slice: decorator syntax lowers to normal calls for functions
  and classes; `property.getter`/`setter`/`deleter` return updated property
  descriptors and Python golden tests cover `@property`, `@staticmethod`, and
  `@classmethod`.
- Done in current slice: iterator protocol foundation is now slot-based;
  `StopIteration`, `iter`, `next`, sequence iterators, dict-key iterators, and
  `list(iterable)` all use object protocol dispatch.
- Done in current slice: `object_delete_attr` now deletes module/type/instance
  attributes and dispatches data descriptor deletion; `property.deleter` uses
  the same descriptor path.
- Done in current slice: `__getattr__` fallback is centralized in
  `object_get_attr` for instances, types/metatypes, and modules.
- Done in current slice: slot wrappers are script-visible descriptors for the
  current slot families; class access returns `wrapper_descriptor`, instance
  access returns `method-wrapper`, and script golden tests cover sequence,
  mapping, iterator, numeric, comparison, `repr`, and `hash` behavior.
- Done in current slice: `repr()` and `hash()` are native builtins using shared
  protocol helpers, and core singleton/type names are present in builtins.
- Done in current slice: `__setattr__` and `__delattr__` are normal object
  method descriptors; public attribute mutation checks user overrides before
  default descriptor/dict behavior, and VM `STORE_ATTR`/`DELETE_ATTR` inherit
  the hook semantics without opcode changes.
- Done in current slice: exposed exception types in builtins and added basic
  type-call constructors for `int`, `bool`, `tuple`, and `dict`; `tuple`
  reuses the iterator protocol collector shared with `list`.
- Next: continue through `global`/`nonlocal`/closures, `__prepare__`,
  syntax-level `metaclass=`,
  full traceback objects, runtime factory root audits, builtin protocol
  cleanup, `for`/call expansion, defaults/keyword-only arguments, and
  descriptor/metaclass completeness.

## 2026-05-19 Core Builtin Constructors

Pre-change review:

- After exposing core type names in builtins, `int`, `bool`, `tuple`, and
  `dict` needed real type-call construction paths. Leaving them present but
  not constructible would turn the builtins module into a misleading partial
  API and force scripts back to literal-only tests.

Completed:

- Added `int()` construction for empty, integer/bool, and decimal string
  inputs. Invalid decimal strings now raise `ValueError`.
- Added `bool()` construction through truth protocol dispatch and singleton
  return values.
- Added `tuple()` and `tuple(iterable)` construction. Tuple now shares the
  iterator-consumption helper used by `list(iterable)`.
- Added empty `dict()` construction through the dict type `new_object`/`init`
  slots.
- Added `ValueError` to the exception hierarchy and exposed the core exception
  names in builtins alongside `StopIteration`.
- Added C++ and Python golden coverage for constructor calls and builtins
  namespace entries.

Design review:

- Constructors are implemented as type `new_object`/`init` slots, not as
  native functions named `int` or `dict`. This preserves the type-call path as
  the single construction protocol for builtin and future heap/extension
  types.
- Iterable constructor behavior is protocol-based: the shared collector uses
  `object_iter` and `object_next`, so future `for` lowering and call expansion
  can reuse the same assumptions.

Known debt and next risk:

- `dict(mapping_or_iterable, **kwargs)`, keyword-aware argument parsing,
  `tuple` exact-object fast return, and richer numeric conversion protocols
  remain open.

Verification:

- `cmake --build build -j2`
- `ctest --test-dir build --output-on-failure`

Result: all 160 tests pass.

## 2026-05-19 Attribute Mutation Hooks

Pre-change review:

- Reads already had `__getattribute__` and `__getattr__`, but writes and
  deletes still went straight through descriptor/dict default behavior. Leaving
  that split in place would make future user classes and metaclasses observe
  different semantics depending on whether access came from VM opcodes or host
  helpers.

Completed:

- Split default mutation into `object_set_attr_default` and
  `object_delete_attr_default`.
- Added `object.__setattr__` and `object.__delattr__` method descriptors that
  call the default APIs.
- Public `object_set_attr` and `object_delete_attr` now check type-level
  `__setattr__` / `__delattr__` overrides first, with scoped roots around
  object/name/value operands before hook calls or allocation-heavy default
  paths.
- Added C++ tests for override ordering and direct `object.__setattr__` /
  `object.__delattr__` default behavior.
- Added `Tests/python/attribute_hooks.py` golden coverage proving script-level
  assignment and `del obj.attr` run hooks.

Design review:

- `STORE_ATTR` and `DELETE_ATTR` remain thin public object API calls. They do
  not know about hooks, descriptors, instance dictionaries, modules, or type
  dictionaries.
- The default APIs are explicit escape hatches for `object.__setattr__`,
  `object.__delattr__`, and internal descriptor code. This mirrors the
  `object_get_attr_default` split and avoids recursion while keeping the
  public API hook-aware.

Known debt and next risk:

- Metaclass-visible assignment now has the same API shape, but richer
  `type.__setattr__` behavior and slot refresh from Python-defined special
  methods still need a dedicated type-ready/update path.
- The builtin/native argument parser is still minimal; `for` lowering and
  `*args`/`**kwargs` expansion remain the next protocol consumers.

Verification:

- `cmake --build build -j2`
- `ctest --test-dir build --output-on-failure`

Result: all 159 tests pass.

## 2026-05-19 Slot Wrapper Descriptors And Builtin Surface

Pre-change review:

- `TypeSlots` had become the single C++ dispatch table, but script-visible
  dunder methods were still absent for builtin protocols. This left `len(x)`
  and `x[i]` on the correct object API while `x.__len__()` and
  `list.__len__(x)` had no coherent descriptor representation.
- Adding these methods as scattered native lambdas in each type dict would have
  duplicated protocol logic and made future C/C++ extension slots harder to
  expose consistently.

Completed:

- Added `wrapper_descriptor` and `method-wrapper` runtime types.
- Added `SlotWrapperObject` and `BoundSlotWrapperObject` with traversal,
  receiver validation, keyword rejection, class access, and instance binding.
- Exposed the current slot families as descriptors: repr/hash, sequence and
  mapping length/get/set/delete/contains/iter, iterator next, int/str numeric
  dunders, and rich comparison dunders.
- Added `repr()` and `hash()` builtins through normal native function objects,
  and exposed `None`, `True`, `False`, `int`, `bool`, `tuple`, and `dict` in
  the builtins module dict.
- Added C++ tests for descriptor binding, unbound calls, receiver validation,
  keyword rejection, iterator wrappers, and numeric/comparison wrappers.
- Added `Tests/python/slot_wrappers.py` golden coverage for script-level
  sequence, dict, iterator, numeric, comparison, `repr`, and `hash` paths.

Design review:

- Slot wrappers invoke the owner type's raw slot after descriptor receiver
  validation. VM opcodes still dispatch through `object_length`,
  `object_get_item`, `object_set_item`, `object_delete_item`, `object_iter`,
  `object_next`, `object_binary_op`, and `object_rich_compare`; no concrete
  type logic moved into VM or builtins.
- Builtins remain ordinary module dictionary entries. `repr` and `hash` are
  native callables, not CLI output helpers, and therefore use the same call and
  argument validation path as `len`, `iter`, and `next`.

Known debt and next risk:

- Python-defined special methods do not yet refresh inherited C++ slots, so a
  heap class that defines `__len__` still needs the future type-ready slot
  population path.
- `__setattr__` and `__delattr__` hooks remain absent; attribute assignment
  and deletion currently run descriptors and dictionaries directly through the
  object API.
- Builtins still need a richer argument parser and protocol users such as
  `for`, `*iterable`, and `**mapping`.

Verification:

- `cmake --build build -j2`
- `ctest --test-dir build --output-on-failure`

Result: all 157 tests pass.

## 2026-05-19 `__getattr__` Fallback

Pre-change review:

- Attribute lookup had a clean descriptor priority path but ended directly in
  `AttributeError`. That forced dynamic user attributes to wait for either VM
  special cases or duplicated lookup logic.
- Python methods already bind through descriptors, so `__getattr__` could be
  implemented at the object API boundary without changing opcode behavior.

Debt addressed:

- Added final `__getattr__` fallback inside `object_get_attr`.
- Instance lookup now tries `type.__getattr__` only after data descriptors,
  instance dictionaries, and normal type attributes fail.
- Type-object lookup can fall back to metatype `__getattr__` after normal
  metatype/class lookup fails.
- Module objects can call a module dictionary `__getattr__(name)` when a module
  attribute is absent, matching the same centralized API shape.

Completed:

- Added C++ tests proving instance fallback runs after normal lookup fails and
  module fallback works without descriptor binding.
- Added `Tests/python/getattr_fallback.py` golden coverage for a Python class
  method `__getattr__`, including normal instance attributes taking priority.

Design review:

- `LOAD_ATTR` still calls only `object_get_attr`; it does not know about
  `__getattr__`. This keeps future `__getattribute__`, slot wrappers, and
  metaclass behavior concentrated in the attribute engine.
- The implementation performs special lookup directly through type dictionaries
  to avoid recursively invoking the fallback while looking up `__getattr__`
  itself.

Known debt and next risk:

- Slot wrappers remain absent, so special methods like `__len__`, arithmetic
  dunders, and comparison dunders are not yet script-visible descriptors for
  builtins.
- Delete statement support is currently single-target. Multi-target and
  destructuring `del` should extend the same target-family compiler path rather
  than lowering through synthetic method calls.

Verification:

- `cmake --build build -j2`
- `ctest --test-dir build --output-on-failure`

Result: all 149 tests pass.

## 2026-05-19 Del Statement Lowering

Pre-change review:

- Attribute and descriptor deletion already had a centralized object API, but
  source-level `del` still lacked bytecode lowering. Implementing it through
  parser-injected `__delattr__` or `__delitem__` calls would have split the
  protocol from VM operations and made future slot wrappers harder to reason
  about.
- Item deletion also needed the same slot-family shape as get/set item before
  `del x[i]` could be a real opcode operation.

Completed:

- Added `DELETE_NAME`, `DELETE_FAST`, `DELETE_ATTR`, and `DELETE_SUBSCR`
  bytecodes with opcode metadata and stack effects.
- Added `DeleteStmt` to the parser/compiler AST path. The parser now accepts
  `del` targets through the assignment-target validator, and the compiler emits
  delete opcodes for names, attributes, and subscripts.
- Added `TypeSlots::delitem` and `object_delete_item`.
- Implemented list and dict item deletion through slots; tuple and
  mappingproxy now raise protocol-level `TypeError` for deletion.
- VM dispatch now routes:
  - name deletion through locals/fast-locals semantics,
  - attribute deletion through `object_delete_attr`,
  - subscript deletion through `object_delete_item`.
- Added C++ coverage for opcode metadata, VM dispatch, compiler lowering, list
  and dict deletion, and immutable/mappingproxy error paths.
- Added `Tests/python/delete_statement.py` golden coverage for name deletion,
  list/dict subscript deletion, property deleters, and function local delete.

Design notes:

- `DELETE_ATTR` and `DELETE_SUBSCR` are thin VM protocol dispatches. They do
  not know about properties, list, dict, or mappingproxy internals.
- Delete target handling now shares the same target-family architecture as
  assignment. Future multi-target and destructuring deletion should add target
  recursion there, not a new parser rewrite.
- `del obj.attr` and `del obj[key]` intentionally preserve descriptor and
  slot semantics through `object_delete_attr` and `object_delete_item`.

Verification:

- `cmake --build build -j2`
- `ctest --test-dir build --output-on-failure`

Result: all 152 tests pass.

## 2026-05-19 `__getattribute__` Protocol

Pre-change review:

- Attribute lookup was centralized, but user-defined `__getattribute__` could
  not intercept reads. Adding more descriptor families before this would have
  made lookup ordering harder to stabilize.
- Directly implementing `__getattribute__` inside `LOAD_ATTR` would have split
  script-visible lookup from host API lookup and repeated recursion hazards.

Completed:

- Split the attribute engine into:
  - `object_get_attr_default`, the default normal lookup used by
    `object.__getattribute__`;
  - public `object_get_attr`, which checks user overrides first and applies
    `__getattr__` fallback after `AttributeError`.
- Added `object.__getattribute__` as a normal method descriptor on `object`.
- User-defined `__getattribute__` now runs before data descriptors, instance
  dict lookup, and non-data descriptors.
- If a user override raises `AttributeError`, public lookup falls through to
  `__getattr__`, matching Python's high-level hook shape.
- Added C++ coverage for override priority, direct
  `object.__getattribute__`, and `AttributeError -> __getattr__` fallback.
- Added `Tests/python/getattribute_override.py` golden coverage through the
  real parser, VM, and script runner.

Design notes:

- The override lookup uses direct type-dictionary search and compares against
  the default `object.__getattribute__` descriptor, so discovering the hook does
  not recursively call the hook itself.
- `object.__getattribute__` intentionally calls the default path only. It does
  not run `__getattr__`; the public lookup wrapper owns that fallback behavior.
- VM `LOAD_ATTR` still calls only `object_get_attr`, preserving the rule that
  opcode dispatch is unaware of descriptor, fallback, or hook internals.

Verification:

- `ctest --test-dir build --output-on-failure -R PythonScripts.RunsGoldenFiles`
- `cmake --build build -j2`
- `ctest --test-dir build --output-on-failure`

Result: all 154 tests pass.

## 2026-05-19 Descriptor Delete Protocol

Pre-change review:

- `property.deleter` could create a descriptor object, but there was no
  attribute-delete API to invoke it. That left descriptor protocol asymmetric:
  get/set were centralized, while delete had no future parser/VM target.
- Dict-backed modules, type dictionaries, and instance dictionaries also had
  no common deletion path, which would have pushed future `del obj.attr`
  lowering into ad hoc VM branches.

Debt addressed:

- Added `DictObject::erase` and centralized `object_delete_attr`.
- Attribute deletion now follows the same priority shape as assignment:
  module dict deletion, metatype data descriptor deletion for type objects,
  type dict deletion, instance data descriptor deletion, then instance dict
  deletion.
- `property` now treats `descr_set(..., value=nullptr)` as descriptor delete:
  it calls the deleter when present and raises `AttributeError` otherwise.

Completed:

- Added C++ coverage for deleting ordinary instance attributes, deleting
  through a data descriptor, and invoking `property.deleter`.
- Covered read-only property delete errors through the same descriptor path.

Design review:

- The parser and VM still do not expose `del` syntax. That is intentional for
  this slice: the object protocol is now the stable target that future
  `DELETE_ATTR` lowering should call.
- Deletion remains in the centralized attribute API. VM opcodes should not
  inspect instance dictionaries, type dictionaries, modules, or property
  objects directly.

Known debt and next risk:

- Add AST target support, `DELETE_ATTR`, `DELETE_NAME`, and eventually
  `DELETE_SUBSCR` using the same target visitor family as assignment.
- Continue descriptor completeness with `__getattribute__`, `__getattr__`, and
  slot wrappers.

Verification:

- `cmake --build build -j2`
- `ctest --test-dir build --output-on-failure`

Result: all 147 tests pass.

## 2026-05-19 Iterator Protocol Foundation And `list(iterable)`

Pre-change review:

- `list()` was still an empty-constructor-only bootstrap builtin. That was
  becoming a lasting API smell because every future iterable consumer would
  either need its own container-specific branch or wait for a proper protocol.
- Sequence and mapping slots had length/getitem/setitem/contains, but no
  iteration protocol. That blocked clean implementations of `for`,
  `list(iterable)`, `*args`, and eventually generator/async iteration routes.

Debt addressed:

- Added `TypeSlots::iter` and `TypeSlots::iternext`, plus centralized
  `object_iter` and `object_next` APIs.
- Added typed iterator runtime objects:
  - `iterator` for index-based sequence iteration over list/tuple-like
    objects.
  - `dict_keyiterator` for dict and mappingproxy key iteration.
- Added `StopIteration` as a real exception type and exposed it in builtins.
- Exposed `iter()` and `next()` through normal native builtin callables.
- Changed `list.__new__` to accept zero or one positional argument and consume
  the argument through `object_iter`/`object_next`. `list.__init__` now accepts
  the same signature so the normal `type.__call__` new/init path remains intact.

Completed:

- Added C++ tests for sequence iterator identity, traversal, `StopIteration`,
  `list(tuple)`, `list(dict)`, and non-iterable TypeError.
- Extended builtins-module tests for `iter`, `next`, and `StopIteration`.
- Added `Tests/python/iterator_protocol.py` golden coverage for script-level
  `iter`, `next`, `list(list)`, `list(tuple)`, and dict key iteration.

Design review:

- `list(iterable)` does not inspect concrete list/tuple/dict layouts. It only
  asks for an iterator and consumes it until `StopIteration`, keeping future
  user-defined iterators, `for`, unpacking, and call expansion on the same
  protocol path.
- Popped or locally held objects during construction are rooted across
  allocation and protocol calls: the iterable, iterator, and collected items
  remain runtime roots until the resulting list owns the items.
- Iterator types are ordinary typed objects with traversal, `iter`, and
  `iternext` slots. They are bootstrap seeds because `for` and call expansion
  need a stable C++ protocol before Python-level iterator classes exist.

Known debt and next risk:

- `for`/`FOR_ITER` lowering should use `object_iter` and `object_next`, not
  direct sequence indexing.
- Call expansion must consume `*iterable` through this iterator protocol and
  validate `**mapping` through mapping APIs.
- Native argument parsing still needs optional/default/keyword-only support.

Verification:

- `cmake --build build -j2`
- `ctest --test-dir build --output-on-failure`

Result: all 145 tests pass.

## 2026-05-19 Decorator Syntax And Property Helper Methods

Pre-change review:

- Descriptor runtime objects existed for `property`, `staticmethod`, and
  `classmethod`, but Python class bodies still had to assign them manually.
  That left real decorator syntax untested and encouraged future parser or VM
  special cases.
- `property` lacked its standard helper methods, so the idiomatic
  `@name.setter` class-body rebinding path could not share the same descriptor
  object model as direct construction.

Debt addressed:

- Added lexer/parser support for decorator lines before `def` and `class`.
- Stored decorators on function/class AST nodes and included them in symbol
  collection.
- Lowered decorators in the compiler as ordinary expression evaluation plus
  repeated `CALL 1`, applying the bottom-most decorator first and rebinding the
  decorated result through the existing name-store path.
- Added `property.getter`, `property.setter`, and `property.deleter` as normal
  method descriptors on the `property` type. Each helper returns a fresh
  `PropertyObject` preserving the other accessor slots and doc object.

Completed:

- Added C++ coverage for property helper methods returning updated descriptor
  objects.
- Added `Tests/python/decorators.py` golden coverage for function decorators,
  class decorators, `@property`, `@value.setter`, `@staticmethod`, and
  `@classmethod`.

Design review:

- Decorator syntax remains purely compile-time lowering to the callable
  protocol. The VM does not know about decorator semantics, and descriptors
  continue to work through ordinary class namespace rebinding and attribute
  lookup.
- Property helper methods are descriptors in `property.__dict__`; they do not
  require parser/compiler knowledge of `property`.

Known debt and next risk:

- Descriptor delete semantics, `__getattribute__`, `__getattr__`, and slot
  wrappers remain open.
- The next builtin cleanup should add an iterator protocol foundation so
  `list(iterable)` can be implemented through protocol dispatch instead of a
  type-specific shortcut.

Verification:

- `cmake --build build -j2`
- `ctest --test-dir build --output-on-failure`

Result: all 143 tests pass.

## 2026-05-19 VM Temporary Root Guard Adoption

Pre-change review:

- Active frames already participate in traversal, but several opcodes popped
  objects off the frame value stack before calling object protocols or
  allocating aggregate results.
- Paths such as `BUILD_LIST`, `BUILD_MAP`, `CALL`, `CALL_KW`, attribute access,
  subscript access, comparisons, truth tests, and class creation could therefore
  leave live operands only in C++ local variables. That is harmless with the
  current non-collecting heap, but unsafe once allocation can trigger GC.

Debt addressed:

- Added scoped `Rooted<Object>` guards in VM dispatch for popped operands before
  protocol calls such as attr/subscript/binary/unary/compare/truth operations.
- Rooted collection literal elements while constructing list/tuple/dict
  objects, and rooted newly allocated aggregate objects until pushed back onto
  the frame stack.
- Rooted callable, positional args, and keyword dict across `CALL`/`CALL_KW`
  dispatch.
- Rooted class body code, bases tuple, prepared namespace, and created type
  across `MAKE_CLASS`.
- Rooted function code and function object across `MAKE_FUNCTION`.

Completed:

- Added bytecode coverage that observes root guards from inside a native
  callable during `CALL`, proving callable and positional args are runtime
  roots while dispatch is in progress and are released afterwards.
- Re-ran the full suite.

Design review:

- The VM now treats “popped from frame stack but still semantically live” as an
  explicit root lifetime. This is the correct boundary for a future tracing or
  moving collector because opcode handlers remain responsible for their own C++
  temporaries.
- This does not claim the whole runtime is GC-safe yet. Python function
  argument binding, runtime factories, descriptor calls, and class construction
  internals still need the same scrutiny.

Known debt and next risk:

- Audit `PythonFunctionObject::call`, `Runtime::new_class`, and builtin
  descriptor constructors for temporary containers that allocate while holding
  unrooted objects outside active frames.
- Introduce collector stress tests once the GC machinery can run during
  allocation.

Verification:

- `cmake --build build -j2`
- `ctest --test-dir build --output-on-failure`

Result: all 142 tests pass.

## 2026-05-19 Shared Native Argument Validation

Pre-change review:

- `print`, `len`, `str`, `list`, `property`, `staticmethod`, `classmethod`,
  `type.__call__`, and default object initialization each carried their own
  small argument-count and keyword-rejection branches.
- The duplicated logic was not catastrophic, but it was the wrong shape for
  the next milestones: keyword-only parameters, default values, C/C++
  extension call helpers, and CPython-like diagnostics need one reusable
  validation layer.

Debt addressed:

- Added internal `object_utils` helpers:
  - `has_keyword_arguments`
  - `require_no_keywords`
  - `require_no_positional`
  - `require_positional_count`
  - `require_positional_at_most`
- Replaced local validation branches in builtins, builtin constructors,
  descriptor constructors, and default object initialization.
- Kept existing script-visible error messages stable while concentrating their
  formatting in one place.

Completed:

- Added C++ coverage proving `str`, `property`, and `staticmethod` preserve
  their argument validation messages through the shared helpers.
- Re-ran all C++ and Python golden tests.

Design review:

- This is intentionally a foundation, not the final CPython argument parser.
  The helpers cover current exact/no/at-most positional checks and keyword
  rejection without introducing an overbuilt parser before defaults and
  keyword-only parameters exist.
- Future native argument parsing should extend this layer rather than adding
  per-builtin local branches.

Known debt and next risk:

- Add optional positional, named keyword, keyword-only, default, and conversion
  helpers when builtins start accepting richer signatures.
- `list(iterable)` and iterator protocol remain separate semantic work.
- Root guards around allocation-heavy VM/runtime paths remain the next
  runtime-safety debt.

Verification:

- `cmake --build build -j2`
- `ctest --test-dir build --output-on-failure`

Result: all 141 tests pass.

## 2026-05-19 StaticMethod And ClassMethod Descriptors

Pre-change review:

- The descriptor engine had centralized lookup and property support, but the
  descriptor family was still too narrow. Without `staticmethod` and
  `classmethod`, Python class tests had to rely only on normal function binding
  and could not exercise class-level method binding semantics.
- `PythonMethodObject` was tied to `PythonFunctionObject`, which would have
  forced `classmethod` to invent a second one-off bound wrapper. That would
  make later method objects, native descriptors, and C/C++ extension callables
  drift apart.

Debt addressed:

- Added typed `StaticMethodObject` and `ClassMethodObject` with traversal,
  repr, descriptor get, construction slots, runtime factories, bootstrap
  types, and builtins exposure.
- Generalized the existing method object to bind an arbitrary callable plus
  `self`; Python functions still expose the same compatibility accessor for
  tests and current call sites.
- `staticmethod.__get__` returns its wrapped callable for class and instance
  access. `classmethod.__get__` returns a bound method whose `self` is the
  owner type for both class and instance access.

Completed:

- Added C++ descriptor tests for staticmethod/classmethod access, binding,
  calls, and traversal.
- Extended builtins-module tests for `staticmethod` and `classmethod`.
- Added `Tests/python/descriptor_methods.py` golden coverage using class-body
  assignment forms.

Design review:

- Descriptor behavior remains in object/type slots and `object_get_attr`; no VM
  or parser special cases were added.
- Decorator syntax is still a parser/compiler feature for later. The runtime
  descriptor semantics are now ready for it because class-body assignment
  already exercises the same object path decorators will produce.

Known debt and next risk:

- `@decorator` syntax, `property.getter`/`setter` helpers, descriptor delete,
  slot wrappers, `__getattribute__`, and `__getattr__` remain open.
- A reusable native argument parser would remove repeated small constructor
  validation helpers across `property`, `staticmethod`, `classmethod`, and
  builtins.

Verification:

- `cmake --build build -j2`
- `ctest --test-dir build --output-on-failure`

Result: all 140 tests pass.

## 2026-05-19 CodeObject Line Table And Traceback Seed

Pre-change review:

- `CodeObject` had `name` and `filename`, but no instruction-to-source mapping.
  As Python script tests expanded, runtime failures could only report
  `TypeError: ...` or `NameError: ...` with no line information.
- `Frame` tracked the next instruction pointer but not the instruction that had
  just failed. That made it awkward to attach source location at the VM
  boundary without guessing from the advanced instruction pointer.
- Compiler instruction emission was open-coded through direct
  `instructions_.push_back(...)`, so adding line metadata without a central
  helper would have created a high-risk invariant: instruction count and line
  count could silently diverge.

Debt addressed:

- Added a `CodeObject` line table whose size must match instruction count when
  present, plus `line_for_instruction`.
- Parser statements now capture their starting line, and the compiler routes
  every bytecode emission through `emit()` so the line table and instruction
  stream are always updated together.
- VM frames track the last executed instruction index. When a Python exception
  leaves an opcode, the VM attaches filename, code name, and source line to the
  exception if it does not already have one.
- CLI script execution now prints a minimal traceback frame before the final
  exception line. Nested function failures keep their inner function location
  instead of being overwritten by the caller.
- Function and class definition bindings now reuse the same fast-local aware
  binding path as assignment targets, removing a small semantic hole in nested
  function scopes.

Completed:

- Added C++ tests for `CodeObject` line-table metadata and malformed line-table
  validation.
- Added compiler/VM tests for module-level and nested-function runtime error
  source locations.
- Updated CLI and Python golden tests to expect the new minimal traceback
  output, using script-path placeholders for portable golden files.

Design review:

- This is deliberately a traceback seed, not a fake complete traceback system.
  The exception object holds the first failing frame's source location so users
  can debug script tests now, while the architecture still leaves room for a
  real traceback object chain.
- Line metadata belongs to `CodeObject`; CLI and VM do not maintain side
  tables. The compiler emission invariant is local and test-covered.

Known debt and next risk:

- Add real traceback objects and a frame chain once block stack / exception
  propagation semantics are richer.
- `CodeObject` still needs stacksize, freevars/cellvars, code flags beyond
  call shape, and script-visible `code`/`frame` attributes.
- Root guards around allocation-heavy VM paths remain the next runtime-safety
  debt before a collecting GC can be enabled.

Verification:

- `cmake --build build -j2`
- `ctest --test-dir build --output-on-failure`

Result: all 138 tests pass.

## 2026-05-19 Unified Call Arguments And Varargs/Kwargs

Pre-change review:

- Runtime calls used `std::vector<Object *>` directly at every boundary:
  VM `CALL`, `object_call`, Python functions, native functions, method
  descriptors, `type.__call__`, `new_object`, and `init`.
- Adding `**kwargs` on top of that vector-only model would have forced
  keyword handling into each callable family separately and made native
  argument parsing another long-term special case.
- `CodeObject` had only `arg_count` and `varnames`; it could not describe
  whether a function owned `*args` or `**kwargs`.

Debt addressed:

- Added `CallArguments { positional, keywords }` as the shared call carrier.
  `object_call(callable, vector)` remains a positional-only host convenience,
  but slot dispatch uses `CallArguments`.
- Converted call slots, native function calls, Python function calls,
  method-descriptor calls, `type.__call__`, `new_object`, and `init` to the
  unified call path.
- Added `CodeObject` flags for `*args` and `**kwargs`, with stable argument
  layout: positional parameters first, then varargs, then varkeywords, then
  ordinary locals.
- Added `CALL_KW`; the compiler lowers keyword calls by building a keyword dict
  and sending it through the same object call protocol.
- Parser now accepts `def f(a, *args, **kwargs)` and keyword call syntax such as
  `f(a=1)`, rejecting repeated keyword names and positional arguments after a
  keyword argument.
- Python function binding now handles missing required arguments, too many
  positional arguments, duplicate positional/keyword values, unexpected
  keywords, varargs tuple creation, and varkeywords dict creation.

Completed:

- Added C++ tests for `CodeObject` flag exposure, `CALL_KW` metadata/stack
  effect, Python function varargs/varkeywords binding, and keyword binding to
  named parameters.
- Added compiler/VM coverage for `def collect(first, *args, **kwargs)` called
  with positional and keyword arguments.
- Added Python golden coverage in `Tests/python/varargs_kwargs.py`.

Design review:

- Keyword semantics now belong to the call protocol, not to individual VM
  opcodes or a Python-function-only helper. This is the correct base for a
  future native argument parser and C/C++ extension call ABI.
- `CALL_KW` is intentionally structural: it pops a keyword dict and positional
  arguments, then dispatches through `object_call`. It does not inspect Python
  function layout.
- Builtin/native callables currently reject keywords unless they opt in. That
  is explicit and centralized, but the next quality step is a reusable
  argument parser with CPython-like messages and support for optional/keyword
  parameters where the builtin actually accepts them.

Known debt and next risk:

- `*iterable` and `**mapping` call expansion are not implemented yet.
- Default values, keyword-only parameters, positional-only markers,
  annotations, decorators, closures, and `global`/`nonlocal` still belong to
  later function-definition slices.
- `CodeObject` still needs line table, stacksize, freevars/cellvars, and
  traceback-facing metadata.
- RAII root guards remain the highest runtime-safety debt before any moving or
  collecting GC pressure is introduced.
- Descriptor family work remains open: `property`, `staticmethod`,
  `classmethod`, slot wrappers, `__getattribute__`, and `__getattr__`.

Verification:

- `cmake --build build -j2`
- `ctest --test-dir build --output-on-failure`

Result: all 132 tests pass.

## 2026-05-19 Scoped Root Guard Foundation

Pre-change review:

- The runtime exposed manual `add_root` and `remove_root`, but no RAII guard.
  Future GC work would therefore depend on every allocation path remembering
  paired root operations by hand.
- The root vector de-duplicated roots and `remove_root` erased all matching
  entries. That is unsafe for scoped guards because nested guards for the same
  object must behave like counted roots.

Debt addressed:

- Added `Rooted<T>` as a non-copyable, movable RAII guard.
- Added `Runtime::root(T *)` convenience construction.
- Changed runtime roots to counted entries: `add_root` pushes a root entry and
  `remove_root` removes one matching entry.

Completed:

- Added C++ coverage proving scoped roots increment/decrement root count,
  nested roots for the same object are counted independently, and move
  construction does not double-remove.

Design review:

- This is the root model foundation, not a finished GC safety pass. VM and
  runtime code still need targeted adoption around temporary objects that live
  outside active frames while allocations occur.
- The counted root model is deliberately simple and compatible with the
  existing traversal API. A future collector can visit duplicate roots without
  semantic harm; correctness matters more than root-list deduplication here.

Known debt and next risk:

- Apply `Rooted<T>` to allocation-heavy VM paths such as tuple/list/map
  building, function call argument binding, class creation, and exception
  construction before enabling any collector that can run during allocation.
- Traceback/line-table metadata remains open and is the next best risk to
  reduce for debugging larger Python golden tests.

Verification:

- `cmake --build build -j2`
- `ctest --test-dir build --output-on-failure`

Result: all tests pass.

## 2026-05-19 Property Descriptor Builtin

Pre-change review:

- Attribute lookup already had the right descriptor priority skeleton, and
  Python functions bound correctly as methods, but the runtime had no real
  script-visible descriptor family beyond internal method/getset descriptors.
- Without `property`, class-system tests would keep using C++ test-only
  descriptors and miss the real Python path: class namespace stores a
  descriptor object, instance access calls a Python getter, class access returns
  the descriptor itself, and assignment to a read-only descriptor raises
  `AttributeError`.

Debt addressed:

- Added `PropertyObject` as its own runtime object type and bootstrap type.
- Exposed `property` in builtins through the normal type call path.
- Implemented property construction with `fget`, `fset`, `fdel`, and `doc`
  positional slots.
- Implemented descriptor get/set behavior:
  - class access returns the property object
  - instance access calls the getter with the instance
  - setter calls receive instance and value
  - missing getter raises `AttributeError`
  - missing setter raises `AttributeError: can't set attribute`
- Added traversal for getter/setter/deleter/doc so future GC sees property
  references.

Completed:

- Added C++ tests for property descriptor access, setter dispatch, read-only
  assignment, repr, and traversal.
- Added Python golden coverage for class access, instance access,
  `type(a.a)`, read-only assignment failure, and a two-argument setter
  property.

Design review:

- `property` is implemented as a normal typed descriptor in a type dictionary,
  not as compiler syntax or a VM special case. This keeps future
  `staticmethod`, `classmethod`, and slot wrappers on the same architecture.
- The current constructor is intentionally positional-only because keyword
  parameter parsing is not yet generalized. That debt is recorded under the
  native argument parser and function default/keyword-only work.

Known debt and next risk:

- `property.getter`, `property.setter`, and `property.deleter` decorator helper
  methods are not implemented yet.
- Descriptor delete semantics are still absent because attribute deletion is
  not yet in the language subset.
- `staticmethod` and `classmethod` are the next natural descriptor-family
  builtins.

Verification:

- `cmake --build build -j2`
- `ctest --test-dir build --output-on-failure`

Result: all 135 tests pass.

## 2026-05-19 Str Builtin And Object String Conversion

Pre-change review:

- `print` had a local `StrObject` branch and otherwise fell back to repr. That
  was a small bootstrap shortcut, but keeping display behavior inside `print`
  would make every text-facing builtin grow its own conversion rules.
- The `str` type existed but was not exposed in builtins and had no constructor
  path.

Debt addressed:

- Added `object_str` as the shared string conversion helper for the current
  runtime stage: exact strings return their contained text; other objects fall
  back to repr until `__str__`/`tp_str` exists.
- Added `str()` construction through the normal type call path.
- Exposed `str` in the builtins module.
- Changed `print` to call `object_str` instead of branching on `StrObject`.

Completed:

- Added C++ coverage for `object_str` and builtins exposure of `str`.
- Added Python golden coverage for `str()`, `str('text')`, `str(42)`, and
  `str([1, 'x'])`.

Design review:

- This is a deliberate protocol seam: when `__str__` / `tp_str` lands,
  `object_str` is the single place to extend. `print`, future formatting, and
  builtin conversion should not inspect concrete types directly.
- `str()` now uses the existing construction protocol, so VM and parser do not
  special-case the name.

Known debt and next risk:

- `object_str` still falls back to repr because there is no `__str__` slot or
  descriptor-level special method lookup yet.
- `list(iterable)`, iterator protocol, and reusable native argument parsing
  remain open builtin debts.

Verification:

- `cmake --build build -j2`
- `ctest --test-dir build --output-on-failure`

Result: all tests pass.

## 2026-05-18 Function Fast Locals And UnboundLocalError

Pre-change review:

- Function bodies used the same name table and `LOAD_NAME`/`STORE_NAME` opcodes
  as module and class body execution. That meant a function-local assignment
  did not make the name local at compile time.
- A function that read a name before assigning it could incorrectly fall
  through to module globals instead of raising Python's local binding error.

Debt addressed:

- Added `LOAD_FAST` and `STORE_FAST` bytecodes for function-local names.
- Added `UnboundLocalError` as a `NameError` subclass in the runtime exception
  hierarchy.
- Split symbol state by scope kind: module/class scopes remain dynamic
  namespace scopes, while function scopes record parameters and assignment
  targets as fast locals.
- Function `CodeObject::varnames()` now stores parameters first, followed by
  compiler-discovered local names.
- Function-body loads and stores for local names now emit
  `LOAD_FAST`/`STORE_FAST`; dynamic global/builtin reads still use `LOAD_NAME`.

Completed:

- Added opcode metadata tests for `LOAD_FAST` and `STORE_FAST`.
- Added exception hierarchy tests for `UnboundLocalError`.
- Added compiler tests proving assigned function locals use fast-local opcodes,
  parameters remain first in `varnames`, and unbound locals raise
  `UnboundLocalError`.
- Added Python golden coverage for successful local reads and failed unbound
  local reads.

Design review:

- This is the first correct scope-analysis slice, not the full Python symbol
  table. It intentionally avoids pretending closures or declarations exist.
- Nested `def` and `class` bind their names as fast locals in an enclosing
  function, but their bodies are separate scopes. Class bodies remain dynamic
  namespace execution because class locals need descriptor/metaclass semantics,
  not function fast locals.
- `LOAD_NAME` remains the correct opcode for module/class dynamic lookup;
  `LOAD_FAST` is the function-local opcode and must not fall back to globals.

Known debt and next risk:

- Parser/compiler still lack `global` and `nonlocal` declarations.
- Freevars/cellvars and closure objects are still absent.
- `CodeObject` still lacks flags to record optimized/function/class/module
  scope shape, which should land with broader code metadata work.

Next:

- Continue scope work with explicit `global`/`nonlocal` syntax and code flags,
  or move to line-table/traceback metadata if debugging complex scope tests
  becomes the higher leverage risk.

Verification:

- `cmake --build build -j2`
- `build/test_core_objects '--gtest_filter=Bytecode.MetadataNamesAndStackEffects:CoreObjects.RuntimeBootstrapsExceptionHierarchy:Compile.FunctionsUseFastLocalsForAssignedNames:Compile.CompilesFunctionDefinitionsAndReturns:PythonScripts.RunsGoldenFiles'`
- `ctest --test-dir build --output-on-failure`
- `git diff --check`

Result: all tests pass.

## 2026-05-18 Metaclass-Aware Class Construction Request

Pre-change review:

- Class creation had two clean but separate entry points: `MAKE_CLASS` created
  and prefilled a namespace before calling `Runtime::new_class`, while
  three-argument `type(...)` parsed arguments in `object_core.cpp` and called
  the same factory directly.
- Continuing to add metaclass conflict checks, `Meta(...)`, `metaclass=`, and
  `__prepare__` on top of both call sites would have created duplicate class
  creation semantics.

Debt addressed:

- Added `ClassConstructionRequest` carrying name, bases, namespace,
  module fallback, qualname, and explicit metaclass seed.
- Added `Runtime::prepare_class_namespace` so `MAKE_CLASS` no longer owns the
  namespace prefill rules.
- Added runtime metaclass selection with CPython's winner rule: explicit
  metaclass seed first, otherwise select the most derived metaclass among all
  bases; incompatible metaclasses raise `TypeError`.
- `type.__call__` now treats subclasses of `type` as metaclass callables for
  the three-argument form, so `Meta('Base', (), {})` creates a class whose
  runtime type is `Meta`.
- `MAKE_CLASS`, direct `type(name, bases, namespace)`, and
  `Meta(name, bases, namespace)` all converge on `Runtime::new_class(request)`.

Completed:

- Added C++ tests for metaclass selection, inherited metaclass use, and
  metaclass conflict errors.
- Added compiler coverage proving `class Child(Base):` inherits Base's custom
  metaclass when Base was created through `Meta(...)`.
- Added Python golden coverage in `Tests/python/metaclass.py`.

Design review:

- This is intentionally not a fake `metaclass=` syntax slice. The important
  architecture move is that all current class creation paths now share a
  request object and one runtime selector, leaving a single place to add
  `__prepare__`, class keywords, and explicit metaclass syntax.
- `Runtime::new_class(request)` still performs the final heap type allocation
  internally. A later full `type.__new__`/metaclass override implementation
  should preserve the same request boundary while allowing metaclass slots to
  customize creation.

Known debt and next risk:

- `__prepare__` lookup/call is not implemented; class statement namespaces are
  still plain dicts prepared by runtime.
- Parser/compiler do not support class keyword arguments such as
  `class A(Base, metaclass=Meta):`.
- Function scope analysis remains the next high-risk semantic debt.

Next:

- Start real symbol analysis for module/function scopes before defaults,
  closures, or more complex class body behavior expand the incorrect
  `LOAD_NAME` semantics.

Verification:

- `cmake --build build -j2`
- `build/test_core_objects '--gtest_filter=CoreObjects.ClassCreationSelectsMostDerivedMetaclass:CoreObjects.ClassCreationRejectsMetaclassConflicts:CoreObjects.TypeCallCreatesHeapTypesFromNameBasesNamespace:Compile.ClassDefinitionsSelectInheritedMetaclass:PythonScripts.RunsGoldenFiles'`
- `ctest --test-dir build --output-on-failure`
- `git diff --check`

Result: all tests pass.

## 2026-05-18 Type Layout Compatibility And List Literal

Pre-change review:

- Heap class creation could combine builtin compact layouts without a solid
  base check. That was unsafe because `list` and `dict` instances require
  different C++ layouts, while the old code inherited only the primary base's
  slots.
- Script tests still could not construct list literals directly, forcing list
  behavior to be covered through C++ setup or `list()` only.

Debt addressed:

- Added `TypeObject::solid_base` as the runtime's explicit layout root, modeled
  after CPython's solid-base compatibility rule.
- Bootstrap now marks each builtin type's layout root. Heap classes inherit the
  selected base's layout root; pure object-layout heap classes keep
  `object` as their solid base.
- `Runtime::new_class` validates all bases before allocation and rejects
  incompatible native layouts with a Python `TypeError`.
- Class creation now chooses the best layout base for slot inheritance instead
  of blindly copying slots from `bases[0]`.
- Added `ListExpr`, parser support for `[]` and `[expr, ...]`, `BUILD_LIST`
  opcode metadata, VM execution, compiler lowering, and Python golden coverage.

Completed:

- Added C++ tests proving `type("Bad", (list, dict), {})` and derived
  list/dict layout combinations fail early.
- Added bytecode tests for `BUILD_LIST` name, stack effect, and dispatch.
- Added compiler tests for list literals, empty lists, list subscript load, and
  list subscript store.
- Added `Tests/python/lists.py` covering list literal repr, indexing, item
  assignment, `len`, and `append`.

Design review:

- `solid_base` is deliberately stored on `TypeObject`, not inferred from slots,
  because slots are behavior protocol entries while layout compatibility is an
  allocation and memory-safety property.
- This does not implement extension layout declaration yet. The next C/C++
  extension type API must make native layout roots explicit rather than letting
  callers smuggle layout through arbitrary `new_object` slots.
- `BUILD_LIST` mirrors `BUILD_TUPLE`: item expressions are evaluated in source
  order, pushed on the stack, popped into a vector preserving order, and passed
  to `Runtime::new_list`.

Known debt and next risk:

- `list()` still accepts no iterable and there is no iterator protocol path.
- No list comprehension or starred list literal unpacking yet.
- Metaclass selection and `__prepare__` remain the next high-risk class-system
  debt after layout safety.

Next:

- Start metaclass-aware class construction design, keeping `MAKE_CLASS` and
  three-argument `type(...)` on one construction path.

Verification:

- `cmake --build build -j2`
- `build/test_core_objects '--gtest_filter=CoreObjects.TypeFlagsSeparateCompactBuiltinsFromHeapInstances:CoreObjects.ClassCreationRejectsIncompatibleNativeLayouts:CoreObjects.TypeCallCreatesHeapTypesFromNameBasesNamespace:CoreObjects.TypeCallValidatesThreeArgumentClassCreation'`
- `build/test_core_objects '--gtest_filter=Bytecode.MetadataNamesAndStackEffects:Bytecode.DispatchesBuildCollectionLiterals:Compile.CompilesListLiterals:PythonScripts.RunsGoldenFiles'`
- `ctest --test-dir build --output-on-failure`
- `git diff --check`

Result: all tests pass.

## 2026-05-18 Singleton Literal Keywords

Pre-change review:

- `True`, `False`, and `None` were still parsed as ordinary names. That meant
  scripts depended on globals/builtins lookup for Python constants and could
  assign to names that must be syntax-level constants.
- Fixing this in the VM or builtins would have preserved the wrong parser and
  symbol-table semantics. These values need to be lexer/parser AST literals.

Debt addressed:

- Added distinct lexer token kinds for `True`, `False`, and `None`.
- Added `SingletonExpr` AST with explicit `TrueValue`, `FalseValue`, and
  `NoneValue` variants.
- Compiler lowers singleton literals directly to runtime singleton constants.
- Assignment target conversion now rejects singleton literals naturally instead
  of treating them as `NameTarget`.

Completed:

- Added compiler tests proving singleton literals execute without preloaded
  globals and compile to direct constants rather than name lookup at literal
  sites.
- Extended invalid assignment target tests for `True = ...`, `False = ...`,
  and `None = ...`.
- Added Python golden coverage for printing singleton literals, tuple literals
  containing them, and boolean control flow.

Design review:

- The implementation keeps singleton semantics in lexer/parser/compiler, not in
  VM name resolution or builtins. That is the right boundary for Python
  constants and avoids a long-term compatibility trap.
- `SingletonExpr` is deliberately narrow instead of a generic object-constant
  AST. Broader constant folding can come later without making AST nodes hold
  arbitrary runtime object pointers.

Known debt and next risk:

- `list` and set literals are still absent.
- Assignment target diagnostics are currently generic `invalid assignment
  target`; CPython has more specific messages for assigning to constants.
- `global`/`nonlocal` and keyword restrictions for function parameter names are
  still future parser work.

Next:

- Add list literal syntax and `BUILD_LIST`, then use it to cover list methods
  at script level without C++ wrapper setup.
- Continue toward metaclass preparation after literal coverage catches up.

Verification:

- `cmake --build build -j2`
- `build/test_core_objects '--gtest_filter=Compile.CompilesTupleAndDictLiterals:Compile.CompilesSingletonLiteralsWithoutNameLookup:Compile.RaisesSyntaxErrorForInvalidAssignmentTarget:PythonScripts.RunsGoldenFiles'`
- `ctest --test-dir build --output-on-failure`
- `git diff --check`

Result: all tests pass.

## 2026-05-18 Class Base List Syntax

Pre-change review:

- Runtime class creation could represent bases and C3 MRO, but `class`
  statements still always created an implicit `object` subclass.
- Keeping no-base and with-base class statements on different VM protocols
  would split class semantics again.
- Base expressions must be evaluated before class body execution, while the
  class body itself still runs in a fresh namespace.

Debt addressed:

- Extended `ClassDefStmt` with evaluated base expressions.
- Parser now accepts `class A():`, `class A(Base):`, and
  `class A(Base, Right):`.
- Compiler emits base expressions, `BUILD_TUPLE`, class body `CodeObject`, and
  `MAKE_CLASS` for every class statement.
- `MAKE_CLASS` now consumes a bases tuple plus class code object and delegates
  to `Runtime::new_class`.

Completed:

- Updated bytecode metadata and malformed-bytecode coverage for the new
  `MAKE_CLASS` stack contract.
- Added bytecode coverage proving evaluated bases are used for class creation.
- Added compiler coverage for multiple class bases, inherited attributes,
  instance construction, and C3 MRO order.
- Extended Python golden class tests for `class B(A):`, `__bases__`, inherited
  attributes, and instance type repr.

Design notes:

- Class statement, direct `type(...)`, and runtime C++ class creation now share
  the same bases-aware construction API.
- `TypeObject::base` remains a primary-base cache only; `bases` and `mro` are
  the semantic source of truth.

Known debt and next risk:

- No metaclass selection, `metaclass=` keyword, `__prepare__`, decorators, or
  `super` yet.
- Builtin/extension layout conflict checks still need explicit type flags.
- `True`/`False`/`None` still need literal token handling.

Next:

- Add list literal syntax and `BUILD_LIST`.
- Then start metaclass preparation: parse class keywords, introduce selected
  metaclass calculation, and keep all paths routed through `Runtime::new_class`
  or its eventual metaclass-aware successor.

Verification:

- `cmake --build build -j2`
- `build/test_core_objects '--gtest_filter=Bytecode.MetadataNamesAndStackEffects:Bytecode.DispatchesMakeClass:Bytecode.MakeClassUsesEvaluatedBases:Bytecode.RejectsMalformedBytecode:Compile.CompilesMinimalClassDefinitions:Compile.CompilesClassDefinitionsWithBases:PythonScripts.RunsGoldenFiles'`
- `ctest --test-dir build --output-on-failure`
- `git diff --check`

Result: all tests pass.

## 2026-05-18 Tuple/Dict Literals For Type Construction Scripts

Pre-change review:

- Three-argument `type(...)` was covered at object and bytecode levels, but
  script tests could not express `(Base,)` or `{'name': value}` naturally.
- Building tuple/dict literals as precomputed constants would have hidden
  expression evaluation order and blocked dynamic keys/values.
- Literal support needed to be a normal parser -> AST -> bytecode -> VM path.

Debt addressed:

- Added tuple literal AST and parsing for `()`, `(expr,)`, and
  `(expr, expr, ...)`, while preserving parenthesized grouping for `(expr)`.
- Added dict literal AST and parsing for `{}`, `{key: value}`, trailing comma,
  and multiple entries.
- Added `BUILD_TUPLE` and `BUILD_MAP` opcode metadata, stack effects, VM
  execution, and compiler lowering.
- Added Python golden coverage for direct `type('Made', (Base,), {...})` class
  construction through the CLI script path.

Completed:

- Added bytecode tests for collection literal build opcodes.
- Added compiler tests for empty/single/multiple tuple literals and dict
  literals with subscript reads.
- Added `Tests/python/type_dynamic.py` covering direct runtime class creation,
  inherited attributes, `__bases__`, `__module__`, and instance construction.

Design notes:

- `BUILD_MAP` evaluates and pushes key/value expressions in source order, then
  inserts them into the runtime dict in the same order.
- Parenthesized grouping remains distinct from tuple syntax: only an empty
  pair or a comma creates a tuple.

Known debt and next risk:

- `list` and set literals are still absent.
- `True`, `False`, and `None` are not yet keyword/literal AST nodes, so the
  compiler still treats them as names where encountered.
- Class base-list syntax still needs parser/compiler integration with the
  bases-aware class creation runtime.

Next:

- Keywordize `True`/`False`/`None` as literal tokens and reject assignment
  targets that try to bind them.

Verification:

- `cmake --build build -j2`
- `build/test_core_objects '--gtest_filter=Bytecode.MetadataNamesAndStackEffects:Bytecode.DispatchesBuildCollectionLiterals:Compile.CompilesTupleAndDictLiterals:PythonScripts.RunsGoldenFiles'`
- `ctest --test-dir build --output-on-failure`
- `git diff --check`

Result: all tests pass.

## 2026-05-18 Three-Argument Type Construction And C3 Seed

Pre-change review:

- `type(obj)` worked, but `type(name, bases, namespace)` still reported a
  placeholder `TypeError`. That forced class statements and direct type
  construction onto different paths.
- `Runtime::new_class` accepted only a single base, which would have made
  `TypeObject::bases`/`mro` decorative instead of semantic.
- Default `__module__` handling had to come from the active executing frame
  when `type(...)` is called from bytecode, while class statements still pass
  an explicit class namespace.

Debt addressed:

- Implemented three-argument `type(name, bases, namespace)` with validation for
  `str`, `tuple`, and `dict` arguments.
- Expanded `Runtime::new_class` to accept a vector of bases and made the
  single-base overload a convenience wrapper.
- Added C3 MRO calculation for heap type initialization, including duplicate
  base detection and inconsistent MRO errors.
- `type(...)` now uses the active VM frame globals to populate `__module__`
  when the namespace did not provide one.
- Class type dictionaries now expose `__bases__` and ensure `__qualname__`.

Completed:

- Added object-level tests for direct type construction, inherited attributes,
  instance construction, explicit module names, empty bases, multi-base C3 MRO,
  invalid argument types, duplicate bases, and inconsistent MRO.
- Added bytecode-level coverage proving `type(...)` called through a VM frame
  uses the module globals `__name__` for `__module__`.

Design notes:

- `TypeObject::base` remains a primary-base cache. Semantics are driven by
  `bases` and `mro`.
- Slot inheritance still uses the primary base. Layout-conflict validation for
  extension/builtin multiple inheritance remains a distinct type-layout task;
  this slice establishes C3 ordering for heap/object-layout classes.
- Script-level golden coverage for direct `type(...)` waits on tuple and dict
  literal syntax so tests can be written naturally in `Tests/python/`.

Known debt and next risk:

- No metaclass selection/conflict handling or `__prepare__` yet.
- No `class A(Base):` parser/compiler syntax yet, even though the runtime can
  now represent multiple bases.
- Builtin/extension type layout conflict checks need explicit flags before
  arbitrary multiple inheritance over non-object layouts is safe.

Next:

- Add base-list parsing for `class A(Base):` and route it through the same
  bases-aware class creation API.
- Add literal keyword handling for `True`/`False`/`None`.

Verification:

- `cmake --build build -j2`
- `build/test_core_objects '--gtest_filter=CoreObjects.TypeCallCreatesHeapTypesFromNameBasesNamespace:CoreObjects.TypeCallValidatesThreeArgumentClassCreation:Bytecode.TypeCallClassCreationUsesActiveFrameModule'`
- `ctest --test-dir build --output-on-failure`
- `git diff --check`

Result: all tests pass.

## 2026-05-18 Minimal Class Definition Syntax

Pre-change review:

- Python functions and bound methods existed, but scripts still could not build
  a class namespace through `class Name:`.
- Implementing class bodies by writing directly into module globals would have
  broken Python semantics and made descriptors/method binding depend on parser
  shortcuts.
- Class creation needs one runtime entrypoint so future `class` statements and
  three-argument `type(name, bases, namespace)` can share validation,
  namespace copying, module naming, bases, MRO, and metaclass selection.

Debt addressed:

- Added lexer/parser/AST/compiler support for minimal `class Name:` blocks.
- Added `MAKE_CLASS`, which pops a class body `CodeObject`, executes it with a
  fresh class namespace as locals and module globals as globals, then creates a
  heap type through `Runtime::new_class`.
- Added `Runtime::new_class(name, base, namespace)` as the central class
  construction seed for this phase.
- Class body assignments now populate the class namespace; functions defined in
  a class body become ordinary Python function descriptor objects and bind as
  methods when read from an instance.

Completed:

- Added bytecode tests for `MAKE_CLASS` metadata, stack effect, dispatch, and
  malformed bytecode.
- Added compiler tests for class attributes, instance attribute fallback,
  method descriptor binding, and method calls.
- Added Python golden script coverage for class metadata, class/instance
  attribute reads, bound Python methods, and `type(a)` for a user class.

Design notes:

- This is deliberately the smallest correct class statement slice:
  `class Name:` with an implicit `object` base.
- `__module__` and `__qualname__` are written into the class namespace before
  executing the class body. `Runtime::new_class` copies the namespace into the
  type dict and synchronizes the type's module field.
- The VM does not implement descriptor, construction, or attribute semantics
  directly. It executes the class body and delegates creation to runtime/type
  APIs.

Known debt and next risk:

- No base-class syntax, tuple `__bases__`, C3 MRO, metaclass selection,
  `__prepare__`, decorators, `super`, or class keyword arguments yet.
- Three-argument `type(name, bases, namespace)` still reports not implemented;
  it must be routed through the same class creation path before inheritance
  syntax expands.
- Class `CodeObject` metadata still lacks qualname/line table details needed
  for tracebacks and introspection.

Next:

- Implement tuple/dict literal syntax and script-level coverage for direct
  `type(name, bases, namespace)`.
- Then add parser/compiler support for `class A(Base):` on top of the
  bases/MRO-oriented runtime API.

Verification:

- `cmake --build build -j2`
- `build/test_core_objects '--gtest_filter=Bytecode.MetadataNamesAndStackEffects:Bytecode.DispatchesMakeClass:Bytecode.RejectsMalformedBytecode:Compile.CompilesMinimalClassDefinitions:PythonScripts.RunsGoldenFiles'`
- `ctest --test-dir build --output-on-failure`
- `git diff --check`

Result: all tests pass.

## 2026-05-18 Function Definition Syntax

Pre-change review:

- Python function objects and argument metadata existed only through C++ tests.
  Scripts still had no `def` statement, no `return`, and no runtime function
  creation opcode.
- Creating function objects at compile time would capture the wrong globals.
  Function creation must happen while a frame is executing so the VM can bind
  the new function to the current globals dict.
- Returning from outside a function must be a syntax error, not a runtime
  fallback.

Debt addressed:

- Added lexer/parser support for:
  - `def name(positional, params):`
  - function body blocks
  - `return expr`
  - bare `return`
- Added `FunctionDefStmt` and `ReturnStmt` AST nodes.
- Added `MAKE_FUNCTION`, which pops a `CodeObject` and creates a
  `PythonFunctionObject` with the active frame globals.
- Compiler lowers function definitions to:
  - nested function `CodeObject` constant
  - `MAKE_FUNCTION`
  - `STORE_NAME`
- Function bodies compile with their own symbol table and code object metadata.

Completed:

- Added bytecode tests for `MAKE_FUNCTION` metadata, dispatch, and malformed
  bytecode.
- Added compiler tests for:
  - zero-arg functions
  - positional-only one-arg functions
  - explicit return values
  - implicit `None` return
  - `return` outside function syntax error
- Added Python golden script coverage for module-level functions.

Design notes:

- This slice is intentionally positional-only and does not implement defaults,
  keyword calls, closures, `global`, `nonlocal`, annotations, decorators, or
  nested-scope closure cells.
- Function creation now has the right shape for future class bodies: a function
  object is created at runtime and stored in the current namespace, so class
  namespace execution can reuse the same statement compiler.

Next:

- Add class body namespace execution and minimal `class Name:` syntax, using
  function objects already stored in the class namespace as descriptors.
- Then route three-argument `type(name, bases, namespace)` through the same
  class creation API.

Verification:

- `cmake --build build -j2`
- `build/test_core_objects '--gtest_filter=Bytecode.MetadataNamesAndStackEffects:Bytecode.DispatchesMakeFunction:Bytecode.RejectsMalformedBytecode:Compile.CompilesFunctionDefinitionsAndReturns:Compile.FunctionWithoutReturnDefaultsToNone:Compile.RaisesSyntaxErrorForReturnOutsideFunction:PythonScripts.RunsGoldenFiles'`
- `ctest --test-dir build --output-on-failure`
- `git diff --check`

Result: all tests pass.

## 2026-05-18 CodeObject Argument Metadata

Pre-change review:

- Python function objects existed, but function calls only supported zero-arg
  code because `CodeObject` did not carry argcount or local variable names.
- Keeping arguments outside `CodeObject` would split function metadata from the
  executable object and make future `def`, traceback, introspection, and method
  binding brittle.
- Bound Python methods could create a `method` object, but prepended `self`
  could not be bound to a local name yet.

Debt addressed:

- Added `CodeObject::arg_count()` and `CodeObject::varnames()`.
- Added a runtime `new_code(...)` overload that accepts argcount and varnames.
- Code object traversal now visits varnames in addition to constants and names.
- Python function calls bind positional arguments into a fresh locals dict using
  the first `arg_count` varnames before executing the code.
- Bound Python method calls now prepend `self` and can execute code that loads
  the `self` local.

Completed:

- Added bytecode tests for code object argument metadata and traversal.
- Added function tests for:
  - positional arg binding through varnames
  - too few and too many positional argument errors
  - malformed code metadata detection
  - bound method calls returning the receiver through the `self` local

Design notes:

- This is deliberately positional-only and minimal. Defaults, keyword-only
  args, `*args`, `**kwargs`, closures, annotations, and qualnames belong in the
  function-definition slice, with explicit `CodeObject`/function metadata.
- Varnames are object references and must remain traversed so future GC sees
  function/code metadata consistently.

Next:

- Add parser/compiler support for a zero-or-positional-only `def` slice:
  function AST node, nested code object creation, function object creation, and
  `STORE_NAME`.
- Add Python golden scripts for module-level functions and simple methods once
  class body execution exists.

Verification:

- `cmake --build build -j2`
- `build/test_core_objects '--gtest_filter=Functions.*:Bytecode.CodeObjectsExposeArgumentMetadata:Bytecode.CodeObjectsTraverseReferencedConstants'`
- `ctest --test-dir build --output-on-failure`
- `git diff --check`

Result: all tests pass.

## 2026-05-18 Python Function And Method Objects

Pre-change review:

- The runtime had native functions and builtin method descriptors, but no
  Python `function` object and no Python bound `method` object. Without these,
  class methods would either need parser/VM shortcuts or would have to reuse
  builtin method objects incorrectly.
- Function calls must preserve the active VM/thread frame chain. Creating an
  unrelated VM for every Python function call would break future traceback,
  tracing, generators, and GC root expectations.
- Builtin bound methods should remain `builtin_function_or_method`-typed to
  match CPython. The missing type is Python's separate `method` object.

Debt addressed:

- Added runtime `function` and `method` types with normal type dictionaries,
  roots, accessors, factories, and slot tables.
- Added `PythonFunctionObject` holding name, `CodeObject`, and captured globals.
- Added `PythonMethodObject` holding function and receiver.
- Python functions are non-data descriptors:
  - class/type access returns the function unchanged
  - instance access returns a bound Python method
- Python function calls reuse `Runtime::active_vm()` when bytecode is already
  executing, preserving nested frame depth in the same `ThreadState`.
- Host-side calls still create a VM entrypoint when no VM is active.

Completed:

- Added tests proving:
  - Python function objects call code using captured globals
  - function objects traverse code and globals
  - function descriptor binding creates a Python `method`
  - bound methods traverse function and receiver
  - bound method calls prepend `self` through the same function call path
  - nested calls from VM bytecode reuse the active VM and show frame depth 2
- Kept builtin `list.append` bound access as `builtin_function_or_method`.

Design notes:

- This slice intentionally supports only zero-argument code objects because the
  compiler has no function definition syntax, argcount, or varnames yet.
  Argument binding must be added with function definition parsing and
  `CodeObject` metadata, not by inventing ad hoc local-name conventions.
- `Runtime::active_vm()` is execution infrastructure for preserving frame
  continuity; it should not become a script-visible runtime object.

Next:

- Extend `CodeObject` metadata with argcount and varnames before exposing
  `def` syntax.
- Add function definition parser/compiler support as a vertical slice:
  `def f(): ...`, `MAKE_FUNCTION` or equivalent, `STORE_NAME`, then script
  golden tests.
- Then add class body namespace execution and route methods through these
  function/method descriptor objects.

Verification:

- `cmake --build build -j2`
- `build/test_core_objects '--gtest_filter=Functions.*:CoreObjects.RuntimeBootstrapsBuiltinTypes:CoreObjects.RuntimeVisitsRootsAndContainerChildren:CoreObjects.ListAppendIsExposedAsMethodDescriptor'`
- `ctest --test-dir build --output-on-failure`
- `git diff --check`

Result: all tests pass.

## 2026-05-18 Type Introspection Builtin

Pre-change review:

- `object` and `list` were script-visible constructors, but `type(obj)` was
  still unavailable despite `__class__` descriptors and runtime type objects
  already existing.
- Exposing `type` without a correct one-argument path would break normal Python
  introspection and force tests to keep using `obj.__class__` as a workaround.
- Three-argument `type(name, bases, namespace)` must share the future class
  creation and metaclass path, so it should not be faked in this slice.

Debt addressed:

- Exposed `type` in the builtins module.
- `type` call now handles the one-argument introspection form by returning the
  operand's runtime type.
- Invalid arity reports a Python `TypeError`; the three-argument form reports a
  clear not-implemented Python `TypeError` until class creation lands.

Completed:

- Extended construction tests for `type(object())`.
- Extended Python golden coverage for `type(object())` and `type(list())`.
- Updated architecture notes to tie three-argument `type(...)` to the planned
  class statement/metaclass implementation.

Next:

- Implement Python function objects and bound Python methods.
- Then implement class body namespace execution and route both `class` and
  `type(name, bases, namespace)` through one class creation API.

Verification:

- `cmake --build build -j2`
- `build/test_core_objects '--gtest_filter=CoreObjects.TypeCallConstructsObjectsThroughNewInitProtocol:Module.BootstrapsBuiltinsModule:PythonScripts.RunsGoldenFiles'`
- `ctest --test-dir build --output-on-failure`
- `git diff --check`

Result: all tests pass.

## 2026-05-18 Type Construction Protocol

Pre-change review:

- `object_call` could call native functions and method descriptors, but type
  objects were not callable through a central construction protocol.
- Letting future `class` syntax allocate instances directly would duplicate
  construction semantics across parser/compiler, VM, builtin helpers, and
  extension type creation.
- The class-system plan needs a stable `type.__call__ -> __new__ -> __init__`
  shaped path before Python-defined classes, inheritance, or metaclasses land.

Debt addressed:

- Added `TypeSlots::new_object` and `TypeSlots::init`.
- `type` now has a call slot that dispatches construction through the target
  type's `new_object` slot, then invokes `init` when the result is an instance
  of the requested type.
- `object` provides the generic allocation slot inherited by heap types.
- `list` provides a layout-correct constructor slot, so `list()` creates a real
  `ListObject` instead of a generic `Object` carrying `list` as its type.
- Exposed `object` and `list` in the builtins module so construction is visible
  through the normal script path.

Completed:

- Added C++ tests proving:
  - `object_call(object_type, {})` constructs an object instance
  - heap types inherit the generic allocation path and preserve instance dicts
  - an `init` slot receives constructor arguments after allocation
  - `list()` constructs an empty list through the same call protocol
  - unsupported constructor arguments raise Python `TypeError`
- Added a Python golden script covering `object().__class__`, `list()`, and
  `list().__class__`.

Design notes:

- This is the construction protocol foundation, not the full `type()` builtin
  behavior. `type(obj)` and `type(name, bases, namespace)` should land with
  Python class creation and metaclass selection tests.
- `new_object` receives the requested `TypeObject`, so builtins with custom C++
  layout can construct subtype instances without hard-coding VM behavior.
- `init` is deliberately separate from `new_object`; later Python `__init__`
  methods and native extension initializers should plug into the same slot.

Next:

- Add Python function objects and Python bound method objects so methods stored
  in type dictionaries bind through descriptors like builtin method descriptors.
- Add class body code object execution with an explicit namespace before class
  statement syntax.
- Keep arbitrary-precision `int` deferred per current instruction.

Verification:

- `cmake --build build -j2`
- `build/test_core_objects '--gtest_filter=CoreObjects.TypeCallConstructsObjectsThroughNewInitProtocol:Module.BootstrapsBuiltinsModule:PythonScripts.RunsGoldenFiles'`
- `ctest --test-dir build --output-on-failure`
- `git diff --check`

Result: all tests pass.

## 2026-05-18 Chained Comparison Lowering

Pre-change review:

- Parser accepted only one comparison segment and rejected `1 < 2 < 3`.
- Correct Python semantics require short-circuiting and evaluating the middle
  operand exactly once; compiling a chain as `(1 < 2) < 3` or as two independent
  comparisons would be wrong.
- VM lacked stack shuffling and a conditional jump that preserves the false
  comparison result for cleanup.

Debt addressed:

- `CompareExpr` now stores a left operand plus a vector of comparison segments.
- Parser accepts chained comparison operators and still reports malformed chains
  such as `1 < < 3` as syntax errors at the broken operand.
- Added structural stack/control opcodes:
  - `DUP_TOP`
  - `ROT_TWO`
  - `ROT_THREE`
  - `JUMP_IF_FALSE_OR_POP`
- Compiler lowers comparison chains with CPython-style stack preservation:
  middle operands are duplicated for the next comparison, false comparison
  results jump to a cleanup block, and true intermediate results are popped.

Completed:

- Added bytecode metadata, stack-effect, VM dispatch, malformed bytecode, and
  compiler tests for chained comparison lowering.
- Python golden scripts now cover true and false chains, including a false first
  comparison that must not evaluate the later missing name.
- Updated architecture notes for comparison chains and Python class-system
  planning.

Design notes:

- `JUMP_IF_FALSE_OR_POP` has path-dependent runtime stack effect. The current
  metadata function returns the true-path effect `-1`; any future static stack
  verifier must model conditional effects explicitly instead of relying on this
  scalar helper.
- The new stack opcodes are VM structure only. Comparison truth, rich-compare
  dispatch, and errors still flow through `object_rich_compare` and `is_true`.
- Class-system work should proceed through function/bound-method descriptors,
  `type.__call__`, class-body execution namespaces, C3 MRO, and metaclass
  selection rather than ad hoc parser or VM shortcuts.

Next:

- Begin Python class-system foundation with TDD:
  - function object and bound method descriptor semantics
  - `type.__call__`, `__new__`, and `__init__` protocol
  - class statement lowering to body code object and namespace
  - C3 MRO and metaclass conflict tests
- Consider `is`, `is not`, `in`, and `not in` as comparison-family syntax after
  identity and containment opcodes are designed.
- Keep arbitrary-precision `int` deferred per current instruction.

Verification:

- `cmake --build build -j2`
- `build/test_core_objects '--gtest_filter=PythonScripts.RunsGoldenFiles:Compile.CompilesChainedComparisonsWithShortCircuit:Bytecode.MetadataNamesAndStackEffects:Bytecode.DispatchesStackManipulationOpcodes:Bytecode.DispatchesJumpIfFalseOrPop:Bytecode.RejectsMalformedBytecode'`
- `ctest --test-dir build --output-on-failure`
- `git diff --check`

Result: all tests pass.

## 2026-05-18 Ordering Comparisons

Pre-change review:

- `RichCompareOp` and `COMPARE_OP` already supported ordering operands, and
  rich-compare `NotImplemented` dispatch is now complete, but parser/compiler
  syntax was still limited to `==` and `!=`.
- Exposing `<`, `<=`, `>`, and `>=` without builtin ordering slots would make
  normal script comparisons fail despite the VM path being present.
- Chained comparisons still require dedicated short-circuit lowering. Compiling
  `1 < x < 3` as two independent comparisons would be incorrect, so this slice
  keeps explicit rejection.

Debt addressed:

- Added lexer tokens for `<`, `<=`, `>`, and `>=`.
- Parser comparison handling now maps all six comparison operators to
  `RichCompareOp`.
- Parser rejects chained comparisons for all comparison operators, not only
  equality.
- Builtin `int`/`bool` and `str` now provide `rich_compare` slots for equality
  and ordering, returning `NotImplemented` for unsupported operand types.

Completed:

- Added tests proving:
  - `int` and `str` ordering dispatch through `object_rich_compare`
  - mixed unsupported ordering raises one central Python `TypeError`
  - compiler emits `COMPARE_OP` operands for ordering syntax
  - chained ordering comparisons are rejected with `SyntaxError`
  - Python golden scripts cover script-visible ordering results

Design notes:

- Equality still has the low-level `EqualSlot` fallback after rich-compare slots
  decline. Ordering has no default fallback and raises `TypeError`.
- Chained comparisons should land as their own control-flow lowering slice,
  using jumps and stack preservation rather than a parser shortcut.

Next:

- Implement chained comparison lowering with short-circuit semantics.
- Consider `is`, `is not`, `in`, and `not in` as separate comparison-family
  syntax after identity and containment opcodes are designed.
- Keep arbitrary-precision `int` deferred per current instruction.

Verification:

- `cmake --build build -j2`
- `build/test_core_objects '--gtest_filter=CoreObjects.RichCompareProtocolDispatchesBuiltinOrdering:CoreObjects.RichCompareProtocolUsesNotImplementedFallbacks:CoreObjects.RichCompareNotImplementedFallsBackToDefaultResults:Compile.CompilesOrderingComparisons:Compile.CompilesEqualityAndInequality:Compile.RejectsChainedComparisonsUntilJumpSemanticsExist:PythonScripts.RunsGoldenFiles'`
- `ctest --test-dir build --output-on-failure`
- `git diff --check`

Result: all tests pass.

## 2026-05-18 Rich Compare NotImplemented Dispatch

Pre-change review:

- Binary operators now had a proper `NotImplemented` fallback path, but
  `object_rich_compare` still called only the left slot and otherwise fell back
  directly to low-level equality for `==`/`!=`.
- Keeping comparison different from arithmetic would split the object protocol
  model and make ordering operators risky to expose.
- CPython-style rich comparison does not need a separate reflected slot; it
  reuses the other operand's `rich_compare` with operands swapped and the
  operator reflected.

Debt addressed:

- `object_rich_compare` now treats `NotImplemented` as a protocol signal.
- Added reflected rich-compare dispatch using the same `RichCompareSlot`:
  - `a < b` reflects to `b > a`
  - `a <= b` reflects to `b >= a`
  - equality and inequality keep the same operator
- Subclass right-hand rich compare gets priority over the base left-hand slot.
- Equality and inequality still fall back to `object_equal` only after both
  rich-compare candidates decline with `NotImplemented` or are absent.
- Ordering comparisons still raise one central `TypeError` after both sides
  decline.

Completed:

- Added tests proving:
  - left rich-compare `NotImplemented` falls back to right rich-compare
  - reflected ordering flips the comparison operator
  - subclass rich-compare slot has priority
  - both-sides-`NotImplemented` equality falls back to default equality
  - both-sides-`NotImplemented` ordering raises Python `TypeError`

Design notes:

- Rich comparison and binary arithmetic now share the same protocol shape:
  slots return a Python object for handled cases, `NotImplemented` for
  unsupported operand combinations, and raise only for real errors.
- Ordering syntax lands in the next status entry above; chained comparison
  lowering remains intentionally separate.

Next:

- Revisit comparison chaining with control-flow lowering rather than compiling
  it incorrectly.
- Keep arbitrary-precision `int` deferred per current instruction.

Verification:

- `cmake --build build -j2`
- `build/test_core_objects '--gtest_filter=CoreObjects.RichCompareProtocolUsesNotImplementedFallbacks:CoreObjects.RichCompareProtocolPrefersSubclassSlot:CoreObjects.RichCompareNotImplementedFallsBackToDefaultResults:CoreObjects.HeapTypesUseAndInheritSlots:Bytecode.DispatchesCompareOpThroughRichCompareProtocol:PythonScripts.RunsGoldenFiles'`
- `ctest --test-dir build --output-on-failure`
- `git diff --check`

Result: all tests pass.

## 2026-05-18 NotImplemented Binary Dispatch

Pre-change review:

- `reflected_binary_op` and `inplace_binary_op` existed structurally, but there
  was no `NotImplemented` singleton and no central fallback rule. Without this
  slice, individual type slots would have to raise or manually redispatch,
  recreating scattered operator semantics.
- Builtin `int` and `str` raised `TypeError` from their slots for unsupported
  operands. That prevented the right operand from participating in reflected
  dispatch.
- `NotImplemented` must be a real runtime object, visible to scripts and rooted
  like `None`, `True`, and `False`.
- Per user direction, arbitrary-precision `int` remains deferred for now.

Debt addressed:

- Added `NotImplementedType` and the `NotImplemented` singleton.
- Added `Runtime::not_implemented_type()` and `Runtime::not_implemented()`.
- Exposed `NotImplemented` in the builtins module.
- Added `object_inplace_binary_op(...)`.
- Centralized binary operator fallback rules:
  - subclass right reflected slot gets priority
  - left `binary_op` may return `NotImplemented`
  - non-subclass right reflected slot runs after left returns
    `NotImplemented` or has no slot
  - in-place binary dispatch falls back to normal binary dispatch when the
    in-place slot returns `NotImplemented`
  - unsupported operations raise one central `TypeError`
- Builtin `int` and `str` binary slots now return `NotImplemented` for
  unsupported operands or operators instead of raising directly.

Completed:

- Added tests proving:
  - `NotImplemented` has its own type and repr
  - script-level `print(NotImplemented)` resolves through builtins
  - left `NotImplemented` return falls back to right reflected slot
  - subclass reflected slot has priority over base left slot
  - in-place binary slot falls back to ordinary binary protocol
  - unsupported operations still report Python `TypeError`

Design notes:

- `NotImplemented` is now the protocol signal for binary slots. Slots should
  return it when they cannot handle an operand combination; they should raise
  only for real execution errors.
- Rich comparison now has the same quality of treatment in the next status
  entry above.
- In-place syntax/opcodes are not parser-visible yet, but the object API is now
  ready for augmented assignment lowering.

Next:

- Add ordering syntax after rich comparison dispatch is complete.
- Add augmented assignment only after compiler target evaluation order is
  reviewed and tested.
- Keep arbitrary-precision `int` deferred per current instruction.

Verification:

- `cmake --build build -j2`
- `build/test_core_objects '--gtest_filter=CoreObjects.RuntimeBootstrapsBuiltinTypes:CoreObjects.ReprAndExceptionObjects:CoreObjects.BinaryOperatorProtocolDispatchesBuiltinTypes:CoreObjects.BinaryOperatorProtocolUsesNotImplementedFallbacks:CoreObjects.BinaryOperatorProtocolPrefersSubclassReflectedSlot:CoreObjects.InplaceBinaryOperatorFallsBackToBinaryProtocol:PythonScripts.RunsGoldenFiles'`
- `ctest --test-dir build --output-on-failure`
- `git diff --check`

Result: all tests pass.

## 2026-05-18 Operator Protocol Migration And Unary Operators

Pre-change review:

- The public object API still exposed `object_binary_add(...)`, and bytecode
  still exposed `Opcode::BinaryAdd`/`BINARY_ADD`. Keeping those compatibility
  paths would preserve the exact ad hoc surface the operator-family work was
  meant to remove.
- Parser/compiler still had `BinaryAddExpr`, so source syntax was structurally
  add-only even after VM gained `BINARY_OP`.
- Unary operators had no protocol at all. Deferring them would likely recreate
  the same one-off path for `-x` that `binary_add` created for `x + y`.
- Current integer storage is still fixed-width `int64_t`; this is not Python
  integer semantics. Operator work must keep this visible as high-priority
  debt rather than normalizing it as the final numeric model.

Debt addressed:

- Removed old binary-add interfaces from active code:
  - `object_binary_add(...)`
  - `Opcode::BinaryAdd`
  - `BINARY_ADD`
  - `BinaryAddExpr`
- Parser/compiler now use `BinaryExpr` carrying `BinaryOp`.
- Lexer/parser/compiler now support `+`, `-`, and `*` with precedence-aware
  `sum -> term -> unary -> postfix` parsing.
- Added a unary operator family:
  - `UnaryOp`
  - `TypeSlots::unary_op`
  - `object_unary_op(...)`
  - `UNARY_OP` bytecode
  - `UnaryExpr`
- Heap type slot inheritance now carries `unary_op`.
- Builtin `int`/`bool` support unary positive, negative, and invert through the
  shared slot. Unsupported unary operations use centralized Python-style error
  formatting.

Completed:

- Added tests proving:
  - no active source files under `Include/`, `Objects/`, `Python/`, `Parser/`,
    or `Tests/` reference the old binary-add names
  - binary and unary object protocols dispatch on builtin types
  - heap types define and inherit binary and unary slots
  - `BINARY_OP` and `UNARY_OP` metadata, stack effects, operand validation, and
    VM execution work
  - compiler emits `BINARY_OP` for add/subtract/multiply and `UNARY_OP` for
    positive/negative/invert
  - Python golden scripts cover arithmetic precedence and unary operators

Design notes:

- Historical status entries below still mention `binary_add`/`BINARY_ADD`
  because they describe earlier repository states. The active code and current
  architecture no longer expose those names.
- `BinaryOp::TrueDivide`, `FloorDivide`, `Modulo`, and `Power` are protocol
  values but are not parser-visible yet. They should land with their numeric
  semantics and tests, not as syntax-only tokens.
- The fixed-width integer representation is now the largest numeric semantic
  debt: Python-visible integer arithmetic must move toward arbitrary precision
  before division/modulo/power work becomes broad.

Next:

- Design arbitrary-precision `int` storage and overflow-free numeric helpers
  before expanding arithmetic further.
- Add `NotImplemented`, reflected dispatch priority, and in-place binary
  fallback rules as one coherent operator protocol slice.
- Add ordering comparisons only after the richer `NotImplemented` dispatch path
  exists.

Verification:

- `cmake --build build -j2`
- `build/test_core_objects '--gtest_filter=CoreObjects.BinaryOperatorProtocolDispatchesBuiltinTypes:CoreObjects.UnaryOperatorProtocolDispatchesBuiltinTypes:CoreObjects.HeapTypesUseAndInheritSlots:Bytecode.MetadataNamesAndStackEffects:Bytecode.ExecutesIntegerBinaryOpAdd:Bytecode.ExecutesStringBinaryOpAdd:Bytecode.ExecutesGenericUnaryOp:Bytecode.ExecutesGenericBinaryOp:Bytecode.RaisesPythonTypeErrorForUnsupportedBinaryOp:Bytecode.DispatchesBinaryOpThroughTypeSlots:Bytecode.RejectsMalformedBytecode:Compile.CompilesLeftAssociativeAddition:Compile.CompilesBinaryOperatorPrecedence:Compile.CompilesUnaryOperators:Compile.CompilesEqualityAndInequality:Compile.CompilesAssignmentModuleAndStoresGlobals:Compile.ModuleExpressionStatementsAreDiscarded:PythonScripts.RunsGoldenFiles'`
- `ctest --test-dir build --output-on-failure`
- `git diff --check`
- `if rg -n "BinaryAdd|BINARY_ADD|binary_add|object_binary_add|BinarySlot" Include Objects Python Parser Tests -S; then exit 1; else echo "no active binary-add remnants"; fi`

Result: all tests pass; active code has no binary-add remnants.

## 2026-05-18 VM Generic Binary Operator

Pre-change review:

- The C++ object protocol now has a generic `BinaryOp`, but the VM still exposed
  arithmetic as a one-off `BINARY_ADD` path. Leaving that in place would make
  subtraction, multiplication, and later arithmetic syntax repeat the old
  special-case pattern.
- `VirtualMachine::binary_add(...)` was a thin private wrapper with no VM state.
  Keeping it would hide the real dispatch path and invite one wrapper per
  operator.
- Malformed arithmetic opcode operands needed explicit bytecode validation
  before parser/compiler starts emitting broader operator syntax.

Debt addressed:

- Added `Opcode::BinaryOp` with `BINARY_OP` metadata and stack effect.
- `BINARY_OP` decodes its operand as `BinaryOp` and dispatches through
  `object_binary_op`.
- Legacy `BINARY_ADD` now dispatches directly through
  `object_binary_op(..., BinaryOp::Add)` and remains only as compatibility for
  the current compiler/tests.
- Removed the private VM `binary_add` helper.

Completed:

- Added tests proving:
  - opcode metadata exposes `BINARY_OP`
  - `BINARY_OP` has the same stack effect as other binary stack operations
  - VM executes subtract and multiply through generic `BINARY_OP`
  - malformed `BINARY_OP` operands raise `BytecodeError` and unwind active
    frames
  - legacy `BINARY_ADD` still works through the object protocol

Design notes:

- `BINARY_OP` is now the VM-level architecture for arithmetic. New arithmetic
  operators should be added by expanding `BinaryOp` and type slots, not by
  adding opcode-specific VM helpers.
- `BINARY_ADD` is still present because the parser/compiler still has
  `BinaryAddExpr`. The next parser/compiler slice should migrate add emission
  to `BINARY_OP(Add)` and then remove the old add-only AST shape when tests are
  updated.

Next:

- Generalize `BinaryAddExpr` to a precedence-aware `BinaryExpr` carrying
  `BinaryOp`.
- Add lexer/parser/compiler support for `-` and `*`; emit `BINARY_OP` operands
  for `+`, `-`, and `*`.
- Add Python golden script tests for script-visible arithmetic precedence.

Verification:

- `cmake --build build -j2`
- `build/test_core_objects '--gtest_filter=Bytecode.MetadataNamesAndStackEffects:Bytecode.ExecutesGenericBinaryOp:Bytecode.ExecutesIntegerBinaryAdd:Bytecode.DispatchesBinaryAddThroughTypeSlots:Bytecode.RejectsMalformedBytecode'`
- `ctest --test-dir build --output-on-failure`
- `git diff --check`

Result: all tests pass.

## 2026-05-18 Binary Operator Slot Family

Pre-change review:

- Arithmetic was represented as a single `binary_add` slot. That shape would
  force every future operator to add another ad hoc slot, VM helper, compiler
  branch, and test fixture.
- Operator error formatting was already at risk of drifting between core
  dispatch and builtin implementations.
- The VM still has only `BINARY_ADD`; changing parser syntax first would bake
  in more special cases before the object protocol is ready.

Debt addressed:

- Replaced the narrow `BinarySlot binary_add` with a protocol-family shape:
  - `BinaryOp`
  - `TypeSlots::binary_op`
  - `TypeSlots::reflected_binary_op`
  - `TypeSlots::inplace_binary_op`
  - `object_binary_op(...)`
- Kept `object_binary_add(...)` as the compatibility wrapper used by the
  current VM opcode, but it now delegates to `object_binary_op(..., Add)`.
- Heap type slot inheritance now carries the full binary operator slot family.
- Builtin `int` now supports C++-level add/subtract/multiply through the same
  slot, and `str` supports add through the same protocol.
- Consolidated operator symbols and unsupported-operand error formatting in
  `object_utils` so future builtin operator implementations share one message
  path.

Completed:

- Added tests proving:
  - builtin int add/subtract/multiply dispatch through `object_binary_op`
  - builtin str add dispatches through `object_binary_op`
  - heap types can define and inherit the generic binary operator slot
  - existing `BINARY_ADD` still dispatches through the type slot path

Design notes:

- This is intentionally the C++ object-protocol layer only. VM opcode shape and
  parser syntax remain unchanged in this commit.
- `reflected_binary_op` and `inplace_binary_op` are structural hooks, but full
  CPython dispatch still needs a `NotImplemented` object, subclass-priority
  ordering, and in-place fallback rules. Those should land as a complete
  protocol slice, not as scattered special cases.
- `EqualSlot` remains the low-level hash/dict equality contract. The operator
  family is separate from `RichCompareSlot`, which returns Python objects.

Next:

- Replace VM arithmetic special casing with a generic `BINARY_OP`-style opcode
  operand carrying `BinaryOp`, while preserving source compatibility for the
  current `BINARY_ADD` tests until migration is complete.
- Add bytecode metadata, malformed operand validation, stack-effect tests, and
  VM dispatch through `object_binary_op`.
- Then generalize parser/compiler expression AST from add-only binary syntax to
  precedence-aware binary expressions for `+`, `-`, and `*`.

Verification:

- `cmake --build build -j2`
- `build/test_core_objects '--gtest_filter=CoreObjects.BinaryOperatorProtocolDispatchesBuiltinTypes:CoreObjects.HeapTypesUseAndInheritSlots:Bytecode.DispatchesBinaryAddThroughTypeSlots:Bytecode.ExecutesIntegerBinaryAdd:Bytecode.ExecutesStringBinaryAdd:Bytecode.RaisesPythonTypeErrorForUnsupportedBinaryAdd'`
- `ctest --test-dir build --output-on-failure`
- `git diff --check`

Result: all tests pass.

## 2026-05-18 Parser And Compiler `if`

Pre-change review:

- Jump bytecodes existed, but script-level control flow still had no parser or
  compiler path. Keeping control-flow tests only at bytecode level would miss
  tokenizer, block parsing, symbol collection, and CLI integration bugs.
- Python block syntax needs explicit indentation tokens. Treating leading
  whitespace as ordinary skipped trivia would make nested compound statements
  impossible to model cleanly.
- Parser state around compound statements must be explicit. A hidden
  "previous statement was compound" flag would be fragile once `while`,
  function definitions, and class definitions arrive.

Debt addressed:

- Added lexer tokens:
  - `If`
  - `Else`
  - `Colon`
  - `Indent`
  - `Dedent`
- Added minimal indentation stack handling with dedent validation.
- Added `IfStmt` AST node with condition, then body, and else body.
- Parser now handles block suites using `INDENT`/`DEDENT`.
- Parser statement parsing returns an explicit compound-statement marker so
  surrounding block/file parsing does not rely on hidden state.
- Compiler lowers `if`/`else` through `POP_JUMP_IF_FALSE` and
  `JUMP_ABSOLUTE`.

Completed:

- Added tests proving:
  - `if`/`else` compiles to jump opcodes and executes the true branch
  - `if` without `else` skips the body when the condition is false
  - missing indentation after `if` raises `SyntaxError`
  - Python golden script `if_else.py` runs through the real CLI path

Design notes:

- The first syntax slice intentionally supports statement blocks only, not
  single-line suites such as `if x: y = 1`.
- `if` lowering consumes the condition through `POP_JUMP_IF_FALSE`; all truth
  semantics remain in VM/object protocol.
- This block parser is the base for `while`, function definitions, and class
  definitions. Future compound statements should reuse `parse_block`.

Next:

- Add `while` using backward `JUMP_ABSOLUTE`.
- Then begin the CPython-style operator infrastructure work requested by the
  user: first C++ slot/API layer, then VM opcodes, then parser/compiler syntax.
- Continue adding Python golden scripts for user-visible control-flow behavior.

Verification:

- `cmake --build build -j2`
- `build/test_core_objects '--gtest_filter=Compile.CompilesIfElseStatements:Compile.CompilesIfWithoutElse:Compile.RaisesSyntaxErrorForInvalidIfBlock:PythonScripts.RunsGoldenFiles'`
- `ctest --test-dir build --output-on-failure`
- `git diff --check`

Result: all tests pass.

## 2026-05-18 Jump Bytecode And Truth Control

Pre-change review:

- `==` and `!=` now produce Python bool values, but the VM had no structural
  jump opcodes. Adding `if`/`while` syntax before jump bytecode would force
  compiler or parser shortcuts.
- Truth conversion must stay centralized through `is_true` and type truth
  slots. Branch opcodes should not special-case bool, int, list, or any
  concrete runtime type.
- Jump targets need bytecode validation so malformed control flow fails as
  `BytecodeError` and active frames are unwound cleanly.

Debt addressed:

- Added control-flow bytecodes:
  - `JUMP_ABSOLUTE`
  - `POP_JUMP_IF_FALSE`
  - `POP_JUMP_IF_TRUE`
- Added opcode names and stack effects.
- Added `Frame::jump_to(...)` so absolute jumps can move forward or backward.
- VM conditional jumps pop one object and call `is_true(...)`.
- VM validates jump targets before rewriting frame instruction pointers.

Completed:

- Added direct bytecode tests proving:
  - unconditional jumps dispatch correctly
  - false conditional jumps use custom type truth slots
  - true conditional jumps use Python truth conversion
  - malformed jump targets raise `BytecodeError`
  - frame stack depth is restored after jump bytecode errors

Design notes:

- This slice deliberately does not add `if` parser syntax yet. It establishes
  the VM control-flow substrate first so parser/compiler lowering can remain a
  pure bytecode emission concern.
- `JUMP_ABSOLUTE` is enough for the next `if` and `while` slices. A future
  assembler/bytecode builder can add symbolic labels and relative jumps without
  changing VM truth semantics.

Next:

- Add parser/compiler support for minimal `if`/`else` blocks using indentation
  tokens and the new jump opcodes.
- Then implement while loops with backward `JUMP_ABSOLUTE`.
- After control flow is script-visible, continue the broader CPython-style
  operator infrastructure plan from C++ slots to VM to parser.

Verification:

- `cmake --build build -j2`
- `build/test_core_objects '--gtest_filter=Bytecode.MetadataNamesAndStackEffects:Bytecode.DispatchesJumpOpcodesThroughTruthProtocol:Bytecode.RejectsMalformedBytecode'`
- `ctest --test-dir build --output-on-failure`
- `git diff --check`

Result: all tests pass.

## 2026-05-18 Rich Compare Equality

Pre-change review:

- Control flow should not land before comparison semantics have an object
  protocol. Otherwise `if`, `while`, and boolean-oriented opcodes would be
  tempted to call `object_equal` or concrete type logic directly.
- The runtime already had a low-level `EqualSlot` used by dict and hashing.
  Script-level comparison needs a separate protocol that returns Python
  objects, while preserving `EqualSlot` as the internal equality fast path.
- Parser/compiler had no comparison AST. Adding `==` as another binary add-like
  special case would make later comparison chaining and ordering operators
  awkward.

Debt addressed:

- Added `RichCompareOp` and `RichCompareSlot`.
- Added `object_rich_compare(...)` as the central script-level comparison API.
- Added heap-type inheritance for `rich_compare`.
- Added `COMPARE_OP` bytecode metadata, stack effect, and VM dispatch.
- Added lexer/parser/compiler support for `==` and `!=`.
- Added `CompareExpr` so comparison syntax is explicit in the AST.

Completed:

- Added tests proving:
  - heap types inherit low-level `equal` behavior into `object_rich_compare`
    fallback
  - custom `rich_compare` slots dispatch and inherit correctly
  - `COMPARE_OP` dispatches through `object_rich_compare`
  - malformed `COMPARE_OP` operands raise `BytecodeError`
  - compiler emits `COMPARE_OP` for `==` and `!=`
  - Python golden scripts print comparison results through the real CLI path

Design notes:

- `EqualSlot` remains the internal bool equality contract for dict and hash
  behavior. `RichCompareSlot` is the Python-visible comparison contract and
  returns Python objects.
- `object_rich_compare` currently falls back to `object_equal` for `==` and
  `!=` when a type has no rich compare slot. Ordering operators are deliberately
  not exposed by syntax yet.
- Chained comparisons are rejected with `SyntaxError` rather than compiled
  incorrectly. Proper Python chaining requires stack preservation and
  short-circuit jump semantics, so it should land with the next comparison and
  jump-control slice.

Next:

- Add ordering operators only with a complete rich-compare plan for
  `NotImplemented`, reverse/subclass dispatch, and TypeError messages.
- Add jump opcodes and truth conversion before `if`/`while`.
- Continue enriching `Tests/python/` for every script-visible feature.

Verification:

- `cmake --build build -j2`
- `build/test_core_objects '--gtest_filter=CoreObjects.HeapTypesUseAndInheritSlots:Bytecode.MetadataNamesAndStackEffects:Bytecode.DispatchesCompareOpThroughRichCompareProtocol:Bytecode.RejectsMalformedBytecode:Compile.CompilesEqualityAndInequality:Compile.RejectsChainedComparisonsUntilJumpSemanticsExist:PythonScripts.RunsGoldenFiles'`
- `ctest --test-dir build --output-on-failure`
- `git diff --check`

Result: all tests pass.

## 2026-05-18 Python Golden Script Tests

Pre-change review:

- CLI execution was already available, but script-level behavior still mostly
  lived in C++ tests that hand-built source strings. Continuing that pattern
  would make parser/compiler/runtime integration tests noisy and expensive to
  extend.
- `test_cli.cpp` carried local process-spawning helpers. Adding a second
  script test runner by copying those helpers would create low-value test
  infrastructure duplication.
- The current language subset is small, so the first Python script suite needs
  a minimal golden format that works before assertions, comparisons, and
  control flow exist in the language itself.

Debt addressed:

- Added `Tests/python/` as the Python-level golden behavior test directory.
- Added `Tests/cpp/test_python_scripts.cpp`, a single runner that discovers
  all `.py` files in `Tests/python/`.
- Added sidecar golden files:
  - `.py.out` for expected combined stdout/stderr
  - optional `.py.exit` for expected non-zero exit codes
- Extracted CLI process helpers into `Tests/cpp/test_process.hpp` and reused
  them from both CLI tests and the Python script runner.
- Updated the development guide to document `Tests/python/` conventions.

Completed:

- Added initial Python golden tests covering:
  - assignment, integer addition, string addition, and `print`
  - `__name__` initialization for file execution
  - Python `TypeError` reporting through the CLI for `len(1)`

Design notes:

- Script tests intentionally run through the real `cxxpython file.py` path.
  They validate parser, compiler, module setup, VM execution, builtin lookup,
  and CLI error formatting together.
- Expected output is externalized in golden files so future behavior cases can
  be added without writing one C++ wrapper per feature.
- This is not a replacement for focused C++ tests. Internals such as slot
  inheritance, GC traversal, opcode stack effects, and exact object layout
  should remain C++ tests.

Next:

- Add Python golden cases whenever a feature becomes script-expressible.
- Add CPython comparison tooling once enough syntax exists to share snippets
  between CPython and cxxpython.
- Continue compiler/VM technical debt review before adding comparison and
  control-flow opcodes.

Verification:

- `cmake --build build -j2`
- `build/test_core_objects '--gtest_filter=PythonScripts.RunsGoldenFiles:Cli.*'`
- `ctest --test-dir build --output-on-failure`
- `git diff --check`

Result: all tests pass.

## 2026-05-18 Builtin `len`

Pre-change review:

- Sequence and mapping slots already exposed `object_length`, but script code
  had no `len` builtin. Leaving that gap would encourage future parser, VM, or
  tests to call object internals directly instead of using normal name lookup
  and call dispatch.
- Builtins must remain normal runtime objects stored in the `builtins` module
  dictionary. Adding `len` as a VM opcode shortcut would duplicate call
  semantics and bypass module visibility.
- Arity errors need to be Python `TypeError`; type errors for unsupported
  objects should come from `object_length` so the slot protocol owns the
  semantic decision.

Debt addressed:

- Added native builtin `len`.
- Registered `len` in the runtime-owned `builtins` module dictionary.
- Implemented `len(obj)` as a normal native function call that returns an
  `int` from `object_length(obj)`.

Completed:

- Added tests proving:
  - `builtins.len` is present and represented as a native function
  - script-level `len(items)` resolves through normal name lookup and call
    dispatch
  - list length is reported through the object protocol
  - wrong arity raises Python `TypeError`
  - unsupported object type raises the `object_length` TypeError path

Design notes:

- `len` intentionally has no VM special case. It participates in the same
  `LOAD_NAME`/`CALL` path as `print` and future builtins.
- The implementation currently returns the runtime's signed `int` object.
  If future containers can report sizes beyond that range, the runtime should
  add `OverflowError` and mirror CPython's overflow behavior.

Next:

- Continue reviewing compiler architecture: AST source locations, symbol table
  responsibilities, and statement parsing boundaries.
- Add comparison and truth-control primitives only after the rich comparison
  slot family is designed, so `if`, `while`, and boolean operations do not grow
  one-off VM semantics.
- Plan slice objects and slice subscript semantics before extending subscript
  parsing beyond a single key expression.

Verification:

- `cmake --build build -j2`
- `build/test_core_objects '--gtest_filter=Module.BootstrapsBuiltinsModule:Compile.CallsBuiltinLenThroughObjectProtocol:Compile.BuiltinLenRaisesPythonTypeError'`

Result: focused tests pass.

## 2026-05-18 Compiler Subscript And Assignment Targets

Pre-change review:

- Parser/compiler assignment handling had split statement nodes for name
  assignment and attribute assignment. Adding subscript assignment directly to
  that model would grow another ad hoc branch and make later destructuring,
  slice assignment, and augmented assignment harder to design cleanly.
- Subscript bytecode was already tested at the VM level, so the remaining work
  belonged in lexer/parser/AST/compiler integration. The compiler must keep
  dispatching through `BINARY_SUBSCR` and `STORE_SUBSCR`, not concrete list or
  dict checks.
- Syntax errors for invalid assignment targets were reported after parsing the
  right-hand side. That made diagnostics less precise and reflected the old
  parser-level target check.

Debt addressed:

- Replaced `AssignAttrStmt` with a unified `AssignStmt` carrying an
  `AssignTarget`.
- Added assignment target nodes:
  - `NameTarget`
  - `AttributeTarget`
  - `SubscriptTarget`
- Added `TargetVisitor` so symbol collection and bytecode emission no longer
  need separate statement classes for each assignment shape.
- Added `SubscriptExpr` and bracket tokens.
- Parser now converts expression syntax to assignment targets in one explicit
  helper, with invalid targets reported at the assignment operator.
- Bytecode emission now supports:
  - `obj[key]` -> `BINARY_SUBSCR`
  - `obj[key] = value` -> `STORE_SUBSCR`

Completed:

- Added compiler tests proving:
  - list subscript load compiles and executes
  - list subscript store compiles and mutates through object protocol
  - dict subscript load compiles and executes
  - invalid assignment targets raise `SyntaxError` at `=`
  - missing `]` raises a precise `SyntaxError`
- Preserved existing assignment behavior for names and attributes.

Design notes:

- Assignment target compilation preserves the current VM stack contract:
  attribute and subscript targets emit target parts before the value, while
  simple name assignment emits only the value before `STORE_NAME`.
- `AssignTarget` is intentionally separate from `Expr`; an expression can be
  parsed first and then accepted or rejected as a store target. This gives
  future destructuring, slice targets, and augmented assignment a stable place
  to attach without adding more statement variants.
- The compiler uses a scoped pending-value guard while emitting store targets,
  so target compilation cannot leave stale state if a Python error interrupts
  bytecode generation.

Next:

- Continue reviewing compiler architecture: AST source locations, symbol table
  roles, and a clean path for comparisons/calls before user functions land.
- Start planning slice objects and slice subscript semantics before extending
  `SubscriptExpr` beyond a single key expression.

Verification:

- `cmake --build build -j2`
- `build/test_core_objects '--gtest_filter=Compile.CompilesSubscriptLoadAndStore:Compile.CompilesDictSubscriptThroughObjectProtocol:Compile.RaisesSyntaxErrorForInvalidAssignmentTarget:Compile.RaisesSyntaxErrorForUnclosedSubscript:Compile.CompilesAttributeLoadAndStore:Compile.CompilesAssignmentModuleAndStoresGlobals'`
- `ctest --test-dir build --output-on-failure`
- `git diff --check`

Result: all tests pass.

## 2026-05-18 Subscript Bytecode Integration

Pre-change review:

- The object protocol now had `object_get_item` and `object_set_item`, but the
  VM still lacked subscript opcodes. Adding compiler support before VM
  metadata and direct bytecode tests would make parser/compiler bugs harder to
  isolate.
- VM dispatch must not recognize `list`, `dict`, `tuple`, or `mappingproxy`
  directly. Subscript semantics belong to the object protocol slots added in
  the previous slice.
- Store stack order needs to be explicit before compiler integration. This VM
  currently follows the existing attribute-assignment convention: target parts
  are pushed before the value. `STORE_SUBSCR` consumes `object`, `key`, `value`
  and has stack effect `-3`.

Debt addressed:

- Added bytecode opcodes:
  - `BINARY_SUBSCR`
  - `STORE_SUBSCR`
- Added opcode names and stack effects:
  - `BINARY_SUBSCR`: object + key -> value, net stack effect `-1`
  - `STORE_SUBSCR`: object + key + value -> empty, net stack effect `-3`
- VM dispatch now routes:
  - `BINARY_SUBSCR` through `object_get_item`
  - `STORE_SUBSCR` through `object_set_item`

Completed:

- Added direct bytecode tests proving:
  - list subscript load dispatches through object protocol
  - list subscript store dispatches through object protocol
  - dict subscript load dispatches through object protocol
  - dict missing key propagates Python `KeyError`
  - tuple item assignment propagates Python `TypeError`
  - opcode names and stack effects are documented in metadata

Design notes:

- This slice deliberately does not change parser/compiler yet. Subscript
  expressions and assignments should be added only after assignment targets
  are reviewed, because name assignment, attribute assignment, and subscript
  assignment should share a coherent target representation.
- Future compiler support must preserve the VM stack contract above, or change
  `STORE_ATTR`/`STORE_SUBSCR` together with tests if the project chooses to
  realign assignment evaluation with CPython bytecode order.

Next:

- Review and refactor assignment-target AST before adding subscript assignment.
- Add parser/compiler support for subscript expressions using `BINARY_SUBSCR`.
- Add parser/compiler support for subscript assignment using `STORE_SUBSCR`.
- Add a `len` builtin backed by `object_length`.

Verification:

- `cmake --build build -j2`
- `build/test_core_objects '--gtest_filter=Bytecode.MetadataNamesAndStackEffects:Bytecode.DispatchesSubscriptOpcodesThroughObjectProtocol:Bytecode.SubscriptOpcodesRaisePythonErrorsThroughObjectProtocol'`
- `ctest --test-dir build --output-on-failure`
- `git diff --check`

Result: all tests pass.

## 2026-05-18 Sequence And Mapping Slot Family

Pre-change review:

- `list`, `tuple`, `dict`, and `mappingproxy` had direct C++ methods, but no
  unified object protocol for length, subscript, assignment, or membership.
  Adding subscript opcodes directly against concrete types would duplicate
  logic in the VM and block heap/user types from participating.
- `mappingproxy` could expose type dictionaries but had no script-facing
  read protocol yet. That made `C.__dict__['x']` impossible to implement
  cleanly later.
- `dict` misses need Python `KeyError`, not a placeholder `TypeError` or null
  return leaking through higher layers.
- Every new slot family must update heap-type inheritance tests, because slot
  omissions here become quiet long-term bugs.

Debt addressed:

- Added sequence/mapping slots to `TypeSlots`:
  - `length`
  - `getitem`
  - `setitem`
  - `contains`
- Added central object helpers:
  - `object_length`
  - `object_get_item`
  - `object_set_item`
  - `object_contains`
- Implemented the slot family for:
  - `list`
  - `tuple`
  - `dict`
  - `mappingproxy`
- Added runtime-owned `KeyError` under `Exception`, with CPython-like message
  formatting for missing keys.
- Extended heap-type slot inheritance and tests to cover the new slot family.

Completed:

- Added tests proving:
  - list length, getitem, setitem, negative index, and membership
  - tuple length, getitem, membership, and item-assignment `TypeError`
  - list non-integer subscript `TypeError`
  - dict length, getitem, setitem, replacement, membership, and missing-key
    `KeyError`
  - mappingproxy length, getitem, membership, and item-assignment `TypeError`
  - heap types inherit `length`/`getitem`/`setitem`/`contains`
  - `KeyError` is bootstrapped in the exception hierarchy

Design notes:

- The VM still has no subscript opcodes; this slice deliberately establishes
  the object protocol first. Future bytecode must dispatch through these
  helpers rather than branching on concrete list/dict types.
- Slice objects and slice indexing are not implemented yet. They should enter
  through the same `getitem`/`setitem` slots once `slice` semantics are planned
  as a full feature.
- Dict/mappingproxy item access now raises `KeyError`; direct `DictObject::get`
  remains a nullable internal lookup helper for attribute tables and bootstrap
  code.

Next:

- Add bytecode metadata and VM dispatch for `BINARY_SUBSCR` and `STORE_SUBSCR`,
  then parser/compiler support for subscript expressions and assignments.
- Add a `len` builtin through the existing builtin module/call protocol,
  backed by `object_length`.
- Review assignment-target AST before adding subscript assignment to avoid
  growing ad hoc parser checks.

Verification:

- `cmake --build build -j2`
- `build/test_core_objects '--gtest_filter=CoreObjects.HeapTypesUseAndInheritSlots:CoreObjects.MappingSlotsReadWriteAndContainDictItems:CoreObjects.MappingProxySlotsReadAndContainWrappedDictItems:CoreObjects.SequenceSlotsReadAndWriteListAndTupleItems'`
- `ctest --test-dir build --output-on-failure`
- `git diff --check`

Result: all tests pass.

## 2026-05-18 `__dict__` And MappingProxy

Pre-change review:

- `TypeObject::dict` was the correct internal storage for type namespaces, but
  exposing it directly to script-level attribute lookup would make class
  dictionaries freely mutable through `type.__dict__`, unlike CPython.
- Heap instances had lazy instance dictionaries, but no `__dict__` descriptor;
  future user classes would either need a second lookup path or expose attrs
  inconsistently.
- Module dictionaries are typed runtime state and should be visible through
  normal attribute lookup, not by module-specific host-side shortcuts.

Debt addressed:

- Added a typed `mappingproxy` object:
  - wraps a `DictObject`
  - renders as `mappingproxy(<dict repr>)`
  - is truthy when the wrapped mapping is non-empty
  - is unhashable
  - traverses the wrapped dict for GC
- Added `Runtime::new_mappingproxy(...)` and runtime-owned
  `mappingproxy_type`.
- Extended the existing `__dict__` getset descriptor:
  - type objects return a fresh mappingproxy over their type dict
  - heap instances return and lazily create their mutable instance dict
  - modules return their module dict
  - compact builtin instances without `HasInstanceDict` raise `AttributeError`

Completed:

- Added tests proving:
  - `list.__dict__` is a `mappingproxy`
  - mappingproxy reflects later type-dict mutations
  - mappingproxy traverses the wrapped dict
  - mappingproxy is unhashable and does not accept arbitrary attributes
  - heap instance `__dict__` is a mutable dict connected to attribute lookup
  - module `__dict__` returns the module dict
  - `list().__dict__` raises Python `AttributeError`

Design notes:

- Mappingproxy currently exposes C++ inspection of the wrapped dict for tests
  and internals. Script-level write protection remains enforced because no
  item-assignment protocol is exposed on mappingproxy.
- Type dict mutation through `object_set_attr(type, name, value)` is still
  intentionally available as the internal/class-mutation path. Once user class
  syntax lands, metaclass assignment rules and mappingproxy visibility need
  additional tests.
- `__dict__` assignment is read-only for now. Supporting replacement for heap
  instances should wait until dict layout, slot layout, and descriptor
  invariants are designed together.

Next:

- Add a sequence/mapping slot family (`length`, `getitem`, `setitem`,
  `contains`) before implementing script-visible mappingproxy reads such as
  `C.__dict__['x']`.
- Add Python function objects and function descriptors so user-defined methods
  can share the descriptor and call path with builtin methods.
- Start a dedicated review of assignment-target AST design before adding
  subscript assignment or destructuring.

Verification:

- `cmake --build build -j2`
- `build/test_core_objects '--gtest_filter=Attributes.TypeDictIsExposedAsMappingProxy:Attributes.InstanceAndModuleDictDescriptorsExposeMutableDicts:Attributes.CompactBuiltinInstancesDoNotExposeDict'`
- `ctest --test-dir build --output-on-failure`
- `git diff --check`

Result: all tests pass.

## 2026-05-18 `__class__` Getset Descriptor

Pre-change review:

- The previous attribute path treated type-object lookup as class MRO first,
  then metatype lookup. That would make data descriptors inherited by the
  metatype, especially `object.__class__`, lose priority for expressions such
  as `list.__class__`.
- `__class__` must not be a magic case in the VM or a value copied into every
  instance dict. It is a data descriptor rooted in the normal object/type
  attribute model.
- Heap instances now have instance dictionaries, so `__class__` must be a data
  descriptor before user code can shadow it accidentally through normal
  attribute assignment.

Debt addressed:

- Added a typed `getset_descriptor` object with:
  - descriptor get slot
  - descriptor set slot
  - owner traversal for GC
  - CPython-like repr for attributes
- Installed `object.__class__` as a getset descriptor.
- Updated type-object lookup order to respect metatype data descriptor
  priority before class MRO attributes, while preserving class-MRO lookup for
  normal type attributes such as `list.append`.
- Updated type-object assignment to route metatype data descriptor writes
  through `descr_set` before mutating the type dictionary.

Completed:

- Added tests proving:
  - `obj.__class__` returns the object's type
  - heap instances report their heap type
  - `list.__class__` and `object.__class__` return `type`
  - the descriptor stored on `object` has `getset_descriptor` type and
    attribute repr
  - direct type-dict shadowing cannot beat metatype data descriptor priority
  - instance and type assignment to `__class__` go through the descriptor and
    raise Python `TypeError`

Design notes:

- `__class__` assignment currently raises the CPython-style TypeError for all
  objects. Later heap-type reassignment can be opened only after layout
  compatibility checks exist.
- Getset descriptors are now the correct home for typed reflective attributes;
  future `__dict__`, `__name__`, frame/code fields, and mappingproxy-facing
  attributes should build on this rather than adding bespoke lookup paths.
- Type-object lookup is still incomplete: metaclass conflict rules, C3 MRO,
  metaclass `__getattribute__`, and fuller mappingproxy item access remain
  future work.

Next:

- Add `__dict__` visibility through getset/mappingproxy instead of exposing
  raw mutable type dictionaries directly.
- Add sequence/mapping slots as a planned family before exposing more list and
  dict special methods.
- Start planning Python function objects and function descriptors so user
  methods can use the same descriptor/call path as builtin methods.

Verification:

- `cmake --build build -j2`
- `build/test_core_objects '--gtest_filter=Attributes.ReadsClassThroughDataDescriptor:Attributes.ClassDescriptorCannotBeShadowedByInstanceOrTypeAttributes'`
- `ctest --test-dir build --output-on-failure`
- `git diff --check`

Result: all tests pass.

## 2026-05-18 Type Layout Flags And List Method Descriptor

Pre-change review:

- `Object::instance_dict` existed for every object and `object_set_attr`
  created it unconditionally for normal instances. That was an early layout
  debt: compact builtin instances such as `list` would accidentally accept
  arbitrary attributes, unlike CPython.
- Builtin operations such as `ListObject::append` were only C++ member
  functions. Exposing methods later through opcode or builtin-specific helper
  paths would duplicate attribute lookup and drift away from descriptor
  semantics.
- `TypeSlots` must stay a protocol dispatch table. Adding method-shaped slots
  such as `append` would pollute the slot layer and scale poorly as builtin
  methods grow.

Debt addressed:

- Added `TypeFlags` with:
  - `TypeFlagHeapType`
  - `TypeFlagHasInstanceDict`
- Heap types now default to heap layout plus instance dictionaries.
- Compact builtin instances now reject arbitrary attribute writes through the
  central `object_set_attr` path unless a data descriptor handles the write.
- Added a typed `method_descriptor` object with descriptor and call behavior.
- Added bound builtin-method objects that use the existing
  `builtin_function_or_method` type, matching CPython's public split between
  `list.append` and `[].append`.
- Installed `list.append` in `list_type->dict`; both unbound and bound calls
  now route through descriptor binding and generic `object_call`.

Completed:

- Added tests proving:
  - builtin compact instances reject arbitrary attributes without allocating an
    instance dict
  - heap types carry the instance-dict layout flag
  - `list.append` class access returns a `method_descriptor`
  - `list.append(list_obj, item)` appends and returns `None`
  - `list_obj.append(item)` returns a `builtin_function_or_method` callable,
    traverses descriptor and receiver, appends, and returns `None`
  - wrong receiver and wrong argument count raise Python `TypeError`

Design notes:

- `TypeObject::dict` is the Python-visible namespace for builtin methods and
  descriptors. `TypeSlots` remains the low-level protocol table.
- Bound builtin methods are represented by an internal C++ object layout, but
  script-visible type identity is `builtin_function_or_method`; its slots now
  dispatch both plain native functions and bound builtin methods.
- Method descriptor callbacks receive `ObjectSpan`, avoiding vector copies
  when the descriptor strips the receiver from an unbound call.
- Data descriptors still take precedence over the instance-dict permission
  check, so compact builtins can later expose settable descriptors without
  opening arbitrary attributes.

Next:

- Add `__class__` as a descriptor on `object`/`type`, with read support first
  and assignment guarded later by layout compatibility checks.
- Add mappingproxy semantics for `type.__dict__` before exposing broad
  script-level type mutation.
- Expand builtin method descriptors for list sequence protocol (`__len__`,
  `__getitem__`, `__setitem__`) only after sequence/mapping slots are planned
  as a family.
- Design heap subtypes of builtin layouts before allowing `list` subclasses to
  combine list storage with heap instance dictionaries.

Verification:

- `cmake --build build -j2`
- `build/test_core_objects '--gtest_filter=Attributes.*:CoreObjects.List*:CoreObjects.TypeFlagsSeparateCompactBuiltinsFromHeapInstances'`
- `ctest --test-dir build --output-on-failure`
- `git diff --check`

Result: all tests pass.

## 2026-05-18 Attribute Bytecode Integration

Pre-change review:

- Runtime attribute semantics had been centralized in `object_get_attr` and
  `object_set_attr`, but bytecode and compiler still had no attribute path.
  Leaving this gap would invite future opcode implementations to duplicate
  dict/descriptor logic.
- New opcodes must update metadata, stack effect, VM dispatch, compiler
  emission, and tests together.

Completed:

- Added bytecode:
  - `LOAD_ATTR`
  - `STORE_ATTR`
- Added opcode names and stack effects:
  - `LOAD_ATTR`: object -> attribute value, net stack effect 0
  - `STORE_ATTR`: object + value -> empty, net stack effect -2
- VM dispatch now routes attribute load/store through:
  - `object_get_attr`
  - `object_set_attr`
- Parser/compiler now support:
  - attribute expressions such as `obj.field`
  - attribute assignments such as `obj.field = 'ok'`
- Added tests proving direct bytecode and compiled source both use the runtime
  attribute API.

Design notes:

- Attribute assignment target parsing currently classifies `NameExpr` versus
  `AttributeExpr` in the parser. This is acceptable for the current tiny
  grammar, but once subscript assignment, tuple unpacking, or augmented
  assignment appear, assignment targets should become a dedicated AST family
  instead of ad hoc expression checks.
- `STORE_ATTR` evaluates object first and value second, then pops value before
  object in the VM. Keep this order explicit when adding side-effecting
  expressions.
- The CLI still cannot create user objects from script source, so script-level
  attribute integration remains covered through C++ tests until class/function
  syntax lands.

Next:

- Add a dedicated assignment-target AST hierarchy before expanding assignment
  syntax materially.
- Add descriptor delete/read-only behavior before exposing `del obj.attr` or
  property setter/deleter semantics.
- Move toward user class creation and function objects so attribute bytecode is
  exercised by script-level class/method snippets.

Verification:

- `cmake --build build -j2`
- `build/test_core_objects '--gtest_filter=Bytecode.MetadataNamesAndStackEffects:Bytecode.DispatchesLoadAttrAndStoreAttrThroughObjectApi:Compile.CompilesAttributeLoadAndStore'`
- `ctest --test-dir build --output-on-failure`
- `git diff --check`

Result: all tests pass.

## 2026-05-18 Instance Dict And Data Descriptor Priority

Pre-change review:

- `TypeSlots` had begun to grow, but most slot factories still used positional
  aggregate initialization. That is a slot-growth debt: adding a new slot can
  leave confusing placeholder `nullptr` entries or accidentally shift behavior.
- Attribute lookup had descriptor `__get__` but no instance dictionaries or
  `__set__` path. Without those, class/user-object work would immediately need
  a second, lower-quality attribute implementation.
- Dict semantics themselves are solid object-key mappings, but they were not
  yet connected to ordinary object instance state.

Debt addressed:

- Added optional lazy instance dictionaries to `Object`.
- Added `object_set_attr(...)` as the write-side twin of `object_get_attr`.
- Added `TypeSlots::descr_set` and data descriptor detection.
- Updated attribute lookup order for normal instances:
  - data descriptor
  - instance dict
  - non-data descriptor/type attribute
- Base `Object::visit_children` now visits the instance dict after type-specific
  traversal, so future GC sees normal object state through one path.
- Heap-type slot inheritance now includes `descr_set`.
- Converted slot factories and tests to designated `TypeSlots` initialization.

Completed:

- Added tests covering:
  - setting module and type attributes through `object_set_attr`
  - lazy instance dict allocation
  - instance dict traversal
  - instance dict overriding a non-data type attribute
  - data descriptor assignment dispatch
  - data descriptor priority over instance dict
  - inherited `descr_set` slots
- Preserved the existing descriptor `descr_get` behavior and full test suite.

Design notes:

- The current model intentionally gives all plain objects a possible instance
  dict. This is correct enough for heap/user-object groundwork, but future
  builtin compact objects will likely need flags controlling whether an
  instance dict is allowed.
- There is still no delete path, no read-only descriptor error behavior, no
  `__slots__`, no mappingproxy for `type.__dict__`, and no `__getattr__` or
  `__getattribute__` override support.
- `object_set_attr` writes directly to type dicts for type objects. Metaclass
  data descriptors and type attribute assignment rules should be refined before
  exposing broad user class mutation.
- Slot growth must continue to update:
  - `TypeSlots`
  - `Runtime::inherit_slots`
  - focused inheritance tests
  - all slot factories with designated initializers

Next:

- Add descriptor delete/read-only semantics and assignment error tests.
- Add `LOAD_ATTR` / `STORE_ATTR` bytecode only after the runtime API remains
  stable.
- Introduce type flags or layout metadata before optimizing away instance dicts
  for compact builtin objects.

Verification:

- `cmake --build build -j2`
- `build/test_core_objects '--gtest_filter=Attributes.*'`
- `ctest --test-dir build --output-on-failure`
- `git diff --check`

Result: all tests pass.

## 2026-05-18 Descriptor Get Slot Foundation

Pre-change review:

- Central attribute lookup still returned raw objects from type dictionaries.
  That would block property, function method binding, slot wrappers, and class
  descriptors from sharing one lookup path.
- New slots must update heap-type inheritance and tests, because previous slot
  additions exposed inheritance omissions.

Debt addressed:

- Added `TypeSlots::descr_get`.
- `object_get_attr` now invokes descriptors found through type/class MRO and
  metatype MRO lookup.
- Heap types inherit `descr_get` from their base type.

Completed:

- Added descriptor tests proving:
  - instance access calls `descr_get(descriptor, instance, owner)`
  - class/type access calls `descr_get(descriptor, nullptr, owner)`
  - derived descriptor types inherit the slot
- Kept module namespace lookup as direct dict access, while module type
  descriptors can still flow through normal type lookup.

Design notes:

- This is only the `__get__` half of descriptor protocol. There is no
  `descr_set`, no data-vs-non-data priority, no instance dict, and no method
  binding yet.
- Once instance dictionaries exist, `object_get_attr` must implement CPython's
  descriptor priority order: data descriptor, instance dict, non-data
  descriptor/class attribute, then fallback hooks.
- Native function binding should become a descriptor-driven behavior rather
  than special casing functions in attribute lookup.

Next:

- Add instance dictionaries for heap/user objects.
- Add `descr_set` and `object_set_attr`, with tests for read-only descriptor
  assignment and data descriptor priority.
- Move future `property`, bound method, and function object behavior onto this
  descriptor path.

Verification:

- `cmake --build build -j2`
- `build/test_core_objects '--gtest_filter=Attributes.*'`
- `ctest --test-dir build --output-on-failure`
- `git diff --check`

Result: all tests pass.

## 2026-05-18 Central Attribute Lookup Foundation

Pre-change review:

- The previous type-dict slice added storage and traversal, but there was still
  no central attribute lookup API. That would force future VM opcodes, builtin
  helpers, method binding, modules, and C APIs to duplicate lookup behavior.
- `AttributeError` was also missing from the runtime exception hierarchy, so
  attribute errors would otherwise regress into generic host errors or the
  wrong Python exception type.

Debt addressed:

- Added runtime-owned `AttributeError` under `Exception`.
- Added centralized attribute lookup:
  - `object_get_attr(Object *, StrObject *)`
  - `object_get_attr(Object *, const std::string &)`
  - `type_lookup_attribute(TypeObject *, StrObject *)`
- Attribute lookup now covers:
  - module dictionaries first
  - class/type MRO dictionaries
  - metatype MRO dictionaries for type objects
  - normal instance lookup through the instance type MRO
- Missing attributes now raise Python `AttributeError`.

Completed:

- Added focused attribute tests for:
  - module dict lookup
  - type dict lookup
  - inherited type attributes through MRO
  - metatype attributes
  - instance attributes resolved through type MRO
  - missing attribute `AttributeError`
- Added `Tests/cpp/test_attributes.cpp` to the GTest suite.

Design notes:

- This is a foundation slice, not a full descriptor implementation. There is
  intentionally no instance dict, data descriptor precedence, non-data
  descriptor binding, `__getattr__`, `__getattribute__`, or mappingproxy yet.
- The API shape keeps future descriptor work centralized: VM opcodes, native
  functions, and extension APIs should call `object_get_attr` rather than
  reading dicts directly.
- The current class-object lookup is simplified but forward-compatible:
  class/type MRO dicts are checked before metatype dicts because descriptor
  priority has not been introduced yet. Descriptor slots must refine this
  ordering before user-visible classes become broad.

Next:

- Add descriptor slots (`descr_get`, `descr_set`) and tests for data vs
  non-data descriptor priority.
- Add object instance dictionaries before user class instances are exposed.
- Add `LOAD_ATTR` bytecode and parser support only after attribute semantics
  stay centralized.

Verification:

- `cmake --build build -j2`
- `build/test_core_objects '--gtest_filter=Attributes.*:CoreObjects.RuntimeBootstrapsExceptionHierarchy'`
- `ctest --test-dir build --output-on-failure`

Result: all tests pass.

## 2026-05-18 Type Dictionaries Foundation

Pre-change review:

- Architecture docs require type dicts before attribute lookup, descriptor
  protocol, method tables, user classes, and script-level reflection can be
  implemented cleanly.
- Keeping type metadata in C++ side fields only would force future descriptor
  and builtin-method work to duplicate lookup logic outside the object model.

Debt addressed:

- `TypeObject` now owns a runtime `DictObject *dict`.
- Builtin types receive type dictionaries during runtime bootstrap.
- Heap types receive type dictionaries during `Runtime::new_heap_type`.
- Type traversal now visits the type dictionary, making method/descriptor
  objects reachable through normal object graph traversal.

Completed:

- Type dicts are initialized with:
  - `__name__`
  - `__module__`
- Added tests proving:
  - builtin types have runtime dictionaries
  - type dict metadata is present
  - type traversal reaches the dict
  - heap types get their own dict

Design notes:

- This is not yet attribute lookup. It deliberately adds the storage and
  traversal foundation first, so attribute lookup can later be one central
  runtime API instead of a VM or builtin special case.
- Type traversal includes MRO entries and can therefore include self-cycles.
  Future GC must use a mark set rather than assuming object graphs are trees.
- Type dict bootstrap currently stores only metadata. Builtin methods,
  descriptors, slot wrappers, and class attributes should land in later
  focused slices.

Next:

- Add a central attribute lookup API over type dicts and instance/module dicts.
- Add descriptor slots (`descr_get`, `descr_set`) before exposing property,
  method binding, or user classes.
- Revisit type dict visibility once `type.__dict__` mappingproxy semantics are
  introduced.

Verification:

- `cmake --build build -j2`
- `ctest --test-dir build --output-on-failure`

Result: all tests pass.

## 2026-05-18 Active Frame Root Traversal

Pre-change review:

- Architecture docs require active frames to be GC-visible roots. The previous
  `ThreadState::visit_frames` implementation only visited value-stack entries,
  which made the frame object, code, locals, globals, and builtins reachable
  only by accident or not at all.
- This would become a serious GC and traceback bug once collection can run
  during bytecode execution.

Completed:

- Changed `ThreadState::visit_frames` to visit active `Frame` objects as the
  first-level roots.
- Kept `Frame::visit_children` responsible for code, locals, globals,
  builtins, and value stack traversal.
- Added a focused VM test proving active frame root traversal reaches the frame
  first and then reaches all frame children through normal object traversal.

Design notes:

- `ThreadState` still owns only an execution stack, not object lifetime. The
  eventual GC integration should ask each active thread state for roots and
  then traverse object children through the regular object protocol.
- This does not yet solve multi-thread registration, inactive frame cleanup,
  block stack traversal, exception state traversal, or traceback retention.

Next:

- Add block stack and exception-state objects before implementing `try`/`except`
  or `with`.
- Design the runtime/thread registry that lets GC discover every active
  `ThreadState`, including future OS-thread-backed execution.

Verification:

- `cmake --build build -j2`
- `build/test_core_objects --gtest_filter=Bytecode.ThreadStateVisitsActiveFramesAsRoots`

Result: focused test passes.

## 2026-05-18 Builtins Module And Callable Protocol

Pre-change review:

- `doc/architecture_design.md` requires builtins to be real module objects and
  forbids VM/CLI special-casing names such as `print`.
- `CALL` belongs in the VM as stack dispatch only; callable semantics must live
  in object/type protocol slots.
- The slot inheritance path must be reviewed with every new slot, because a
  previous `binary_add` change exposed this exact debt.
- Module globals need `__builtins__` before script execution grows more user
  visible behavior.

Debt addressed:

- Replaced the empty per-execution builtins dict shortcut with a runtime-owned
  `builtins` module and shared builtins dict.
- Added `__builtins__` to new module dictionaries.
- Added a generic callable slot and centralized `object_call(...)` helper.
- Added a typed `NativeFunctionObject` for bootstrap native functions.
- Added a minimal `print` builtin without special-casing it in the VM or CLI.
- Added `CALL` bytecode metadata, stack effect, VM execution, AST node,
  parser support, symbol collection, and compiler emission.

Completed:

- `Runtime` now bootstraps:
  - `builtin_function_or_method` type
  - `builtins` module
  - `builtins.print`
  - runtime stdout stream injection for tests and CLI use
- `VirtualMachine::execute(code, globals)` now uses runtime builtins rather
  than allocating an empty dict.
- Parser/compiler support call expressions such as `print('hello', 42)` and
  `1()`.
- `CALL argc` pops arguments in source order, pops callable, calls the object
  protocol, and pushes the result.
- Added tests covering:
  - builtins module bootstrap and module `__builtins__`
  - opcode name and stack effect for `CALL`
  - callable slot dispatch through bytecode
  - non-callable Python `TypeError`
  - compiler emission and execution for call expressions
  - CLI script stdout for `print('hello', 42)`

Design notes:

- `print` currently implements only positional arguments, space separation, and
  trailing newline. That is enough to prove the correct architecture, but not a
  full Python builtin.
- `print` renders `str` objects as raw text and other objects with `repr`
  because there is not yet a `str()` protocol or `__str__` slot. The next text
  protocol slice should remove this limitation through general object
  conversion, not by adding more `print` cases.
- There is still no Python function object, keyword argument protocol, bound
  method call path, or type construction via call. Those should all extend the
  same slot.
- Native function callbacks are currently C++ closures. They must not capture
  GC-managed objects unless traversal/rooting is designed for that use case.
- Runtime stdout injection is a testable bridge toward `sys.stdout` and `_io`;
  once `sys` and file objects exist, `print(file=...)` should write through
  Python-visible stream objects.

Next:

- Split argument passing into an explicit call args structure before adding
  kwargs, defaults, bound methods, or Python function objects.
- Add source locations to AST nodes and eventually instruction line tables so
  call errors and tracebacks point at the right source span.
- Continue VM/frame hardening: active frame roots, block stack, exception
  state, and `CodeObject` stacksize/flags.
- Start the next technical-debt slice from the type system: type dictionaries,
  attribute lookup, descriptor hooks, and class/type call protocol.

Verification:

- `cmake --build build -j2`
- `ctest --test-dir build --output-on-failure`
- `git diff --check`

Result: all tests pass.

## 2026-05-17

Current phase: Milestone 0/1 bootstrap.

Completed:

- Initialized repository and documentation set.
- Added CMake build skeleton.
- Added `cxxpython_core` library.
- Added minimal CLI program.
- Added first TDD slice for core object semantics:
  - `object`/`type` bootstrap.
  - `None`, `bool`, `int`, `str`, `tuple`, `dict`.
  - `bool` as an `int` subtype.
  - `True == 1`, matching hash values, and dict key merging.
  - tuple keys in dict.

Next:

- Start compiler/VM planning only after the foundation remains stable.

## 2026-05-17 Core Object Architecture Hardening

Completed:

- Removed the parallel `ObjectKind` semantic dispatch path.
- Moved core behavior to virtual object methods:
  - `repr`
  - `hash`
  - `equals`
  - `truth`
  - `visit_children`
- Replaced host `std::invalid_argument` error paths in object/dict/hash behavior
  with `PythonError`.
- Kept `bool` as an `int` subtype structurally and semantically.
- Added explicit runtime heap/root APIs:
  - `visit_heap`
  - `visit_roots`
  - `add_root`
  - `remove_root`
  - `heap_size`
  - `root_count`
- Added tests proving custom object semantics can extend behavior without
  editing central switch statements.
- Added tests for Python error paths and child traversal.

Verification:

- `cmake --build build -j2`
- `ctest --test-dir build --output-on-failure`

Result: all tests pass.

## 2026-05-18 Code Metadata And Syntax Locations

Debt addressed:

- Historical entries called out missing `CodeObject` filename/name metadata and
  missing `SyntaxError` source locations. Without these, CLI diagnostics,
  traceback, frames, functions, and script-level introspection would need
  incompatible side channels later.

Completed:

- Added `CodeObject::name()` and `CodeObject::filename()`.
- Added runtime factory overloads for creating named code objects.
- Updated compiler APIs with filename-aware overloads:
  - `compile_eval_input(runtime, source, filename)`
  - `compile_exec_input(runtime, source, filename)`
- Kept existing compiler overloads as `<string>` defaults.
- Added token line/column tracking in the lexer.
- Updated parser and lexer syntax errors to include
  `filename:line:column: message`.
- Updated CLI file execution to pass the script path into compilation.
- Updated code repr to expose code name and filename.
- Added tests covering:
  - code object name/filename for eval and module code
  - SyntaxError location for parser errors
  - CLI SyntaxError location reporting

Design notes:

- Syntax location is currently encoded in the exception message only. This is
  enough to remove the diagnostic blind spot, but future `SyntaxError` should
  expose Python fields such as filename, lineno, offset, text, end_lineno, and
  end_offset.
- Code objects now have filename/name but still lack line tables, varnames,
  freevars/cellvars, stacksize, and flags. Those should arrive before function
  objects and traceback support.
- Architecture docs now spell out VM stack/frame/module/type openness rules so
  future work does not reintroduce hidden side channels.

Verification:

- `cmake --build build -j2`
- `ctest --test-dir build --output-on-failure`

Result: all tests pass.

## 2026-05-18 AST Visitor Compiler Passes

Debt addressed:

- Historical status entries noted that compiler AST handling used repeated
  `dynamic_cast` passes. That would force every new syntax node to update
  multiple ad hoc type-dispatch trees.

Completed:

- Added AST visitor interfaces:
  - `ExprVisitor`
  - `StmtVisitor`
- Added `accept(...)` dispatch to all current AST nodes:
  - integer expression
  - string expression
  - name expression
  - binary-add expression
  - expression statement
  - assignment statement
- Replaced compiler AST `dynamic_cast` dispatch with visitors:
  - `SymbolCollector`
  - `BytecodeBuilder`
- Kept behavior unchanged while making future AST extension points explicit.

Design notes:

- Compiler passes now fail at compile time when a new AST node is added without
  visitor support, instead of silently depending on runtime fallthrough.
- The next AST hardening step is source-location metadata on tokens and nodes;
  visitor structure gives that work a cleaner place to attach diagnostics.
- Runtime-object casts, such as checking that `CodeObject.names` entries are
  strings, remain separate runtime safety checks and are not part of the AST
  dispatch debt.

Verification:

- `cmake --build build -j2`
- `ctest --test-dir build --output-on-failure`

Result: all tests pass.

## 2026-05-18 Frame Name Resolution Hardening

Debt addressed:

- Historical status entries noted that `Frame` had globals only and that
  `LOAD_NAME` resolved only against globals. That would make builtins, locals,
  functions, and module execution semantics diverge quickly.

Completed:

- Extended `Frame` with explicit:
  - locals
  - globals
  - builtins
- Changed frame traversal to visit code, locals, globals, builtins, then stack
  references.
- Added `VirtualMachine::execute(code, locals, globals, builtins)`.
- Kept `execute(code, globals)` as the module convenience path where
  locals and globals are the same dictionary and builtins is currently empty.
- Changed `LOAD_NAME` resolution to search:
  - locals
  - globals
  - builtins
- Changed `STORE_NAME` to write to locals, matching the frame namespace model.
- Added tests for traversal and locals -> globals -> builtins resolution order.

Design notes:

- This is a deliberate bridge toward CPython-like frame semantics without yet
  introducing fast locals, closure cells, or builtins module wiring.
- Builtins is currently an explicit dict supplied by callers or an empty dict
  allocated by the VM convenience path. A real builtins module should replace
  that empty dict before builtin functions are added.
- Module execution remains correct for the current subset because module frames
  pass the same dictionary for locals and globals.

Verification:

- `cmake --build build -j2`
- `ctest --test-dir build --output-on-failure`

Result: all tests pass.

## 2026-05-18 Module Object And Script Globals

Completed:

- Added runtime-owned `module` type and `ModuleObject`.
- Added `Runtime::module_type()` and `Runtime::new_module(name, filename)`.
- Added module globals initialization:
  - `__name__`
  - `__file__`
- Added module repr and traversal of module dictionary.
- Changed `cxxpython file.py` to execute inside a `ModuleObject` globals dict
  instead of a raw standalone dict.
- Added GTest coverage for:
  - module type bootstrap
  - module globals initialization
  - module repr
  - executing code against module globals
  - module traversal
  - CLI scripts using initialized `__name__` and `__file__`

Design notes:

- This removes the raw-dict module execution shortcut from the CLI path.
- The module object is still minimal: no import spec, package, loader,
  `sys.modules`, builtins linkage, or attribute access proxying to module
  dictionary.
- Module metadata is currently stored both as C++ fields and Python globals.
  That is acceptable for this bootstrap layer, but future module attribute
  access should make the dict the Python-visible source of truth.
- `ModuleObject` construction keeps allocation lifecycle clean: dictionary
  setup happens in the runtime factory, while the object constructor only
  stores already-created references.

Next:

- Add `Frame` locals/builtins separation and make name resolution walk
  locals -> globals -> builtins.
- Add a minimal builtins module before implementing `print`.
- Replace compiler AST `dynamic_cast` passes with a visitor once statement
  variety grows beyond this initial assignment/expression subset.

Verification:

- `cmake --build build -j2`
- `ctest --test-dir build --output-on-failure`

Result: all tests pass.

## 2026-05-18 Module Execution And CLI File Entry

Pre-change review:

- GTest is already integrated and remains the default C++ test layer.
- AST risk: expression-only AST was becoming a dead end for module execution.
  This cycle split `Expr`, `Stmt`, and `Module` instead of bolting assignment
  onto `compile_eval_input`.
- Slot risk: arithmetic is still only `binary_add`; no new slot was added this
  cycle, but the next operator work should be protocol-family based.
- `CodeObject` risk: constants/names/instructions are still enough for the
  current subset, but line table, filename/name metadata, varnames, and flags
  must arrive before tracebacks and functions.
- `Frame` risk: frame state now has globals but still lacks locals, builtins,
  block stack, exception state, and line tracking.
- `Runtime` risk: module execution currently uses a raw globals dict because
  there is no `module` object or import system yet.

Completed:

- Added `STORE_NAME` and `POP_TOP` bytecode.
- Added statement parsing and a module AST:
  - expression statements
  - assignment statements
  - newline-separated file input
- Added `compile_exec_input(Runtime&, std::string_view)` for module/script
  input, keeping it separate from `compile_eval_input`.
- Extended compiler emission:
  - expression statements discard results with `POP_TOP`
  - assignments store into frame globals with `STORE_NAME`
  - module execution returns `None`
- Extended VM execution for `STORE_NAME` and `POP_TOP`.
- Added `cxxpython file.py` execution:
  - reads source from disk
  - compiles as exec input
  - executes with a module globals dict
  - reports Python errors to stderr with exit code 1
- Added GTest coverage for:
  - `STORE_NAME`/`POP_TOP` metadata and stack effects
  - assignment storing globals
  - multi-statement module execution order
  - expression statement result discard
  - CLI successful script execution
  - CLI Python error reporting

Design notes:

- The CLI is now a real script entrypoint, but still intentionally minimal:
  there is no `print`, `sys.argv`, `__name__`, `__file__`, builtins dict,
  import machinery, or stdout-visible user behavior beyond error reporting.
- CLI tests currently use POSIX `popen` against the built executable. This is
  useful for proving the actual binary works, but should later be moved behind
  a reusable subprocess test helper before the CLI matrix expands.
- The current symbol table records names for module storage/load but still does
  not classify local/global/free/cell namespaces. Functions must not build on
  this as-is; they need real scope analysis first.
- Parser errors still lack source location and filename. Add this before
  expanding grammar materially, otherwise diagnostics and traceback design will
  have to be retrofitted.

Next:

- Add a minimal `module` object and script globals initialization
  (`__name__`, `__file__`) before importing or richer CLI behavior.
- Add builtin `print` only after callable/function protocol is designed rather
  than special-casing it in the CLI or VM.
- Introduce line/filename metadata in `CodeObject` and `SyntaxError`.

Verification:

- `cmake --build build -j2`
- `ctest --test-dir build --output-on-failure`
- `git diff --check`

Result: all tests pass.

## 2026-05-18 Name Loading And Symbol Table Slice

Completed:

- Added runtime-owned `NameError` to the exception hierarchy.
- Extended bytecode with `LOAD_NAME`.
- Extended `CodeObject` with a names table.
- Extended `Frame` with explicit globals namespace storage and traversal.
- Added identifier lexing/parsing and `NameExpr` AST support.
- Added a first explicit symbol-table pass that interns referenced names before
  bytecode emission.
- Added VM name loading from frame globals with Python `NameError` on missing
  names.
- Added tests covering:
  - opcode metadata for `LOAD_NAME`
  - frame traversal including globals
  - compiled name loads through globals
  - symbol-table entry reuse for repeated names
  - missing-name `NameError`

Design notes:

- `CodeObject.names` is stored as runtime string objects rather than host
  strings, to avoid introducing stringly typed runtime state into executable
  artifacts.
- This is still expression-scope name loading only. There is no assignment,
  local-fast slot layout, closure analysis, or statement/module scope yet.
- The current symbol-table pass records referenced names but does not classify
  them into local/global/free/cell namespaces. The first assignment/function
  milestone must replace this with a real scope analysis phase instead of
  layering ad hoc flags onto the current table.
- `LOAD_NAME` currently resolves only against frame globals. Builtins fallback,
  locals-vs-globals precedence, and closure environments should be added
  together as a coherent name-resolution model.

Verification:

- `cmake --build build -j2`
- `ctest --test-dir build --output-on-failure`
- `git diff --check`

Result: all tests pass.

## 2026-05-18 Eval Compiler Bootstrap Slice

Completed:

- Added a first real source-to-bytecode path:
  - `compile_eval_input(Runtime&, std::string_view)`
  - `Parser/lexer.cpp`
  - `Parser/parser.cpp`
  - `Python/compiler.cpp`
- Added a minimal expression grammar:
  - integer literals
  - single-quoted string literals with basic escapes
  - left-associative `+`
- Added AST nodes for integer, string, and binary-add expressions.
- Added bytecode emission to runtime-owned `CodeObject` values.
- Added runtime-owned `SyntaxError` to the exception hierarchy.
- Added compiler tests proving:
  - integer literal compilation and execution
  - left-associative addition compilation and execution
  - string literal addition compilation and execution
  - malformed input raises Python `SyntaxError`
  - unexpected trailing tokens raise Python `SyntaxError`

Design notes:

- This is the first self-bootstrap-oriented vertical slice:
  source -> lexer -> parser/AST -> compiler -> code object -> VM.
- The public compiler API is intentionally named `compile_eval_input` because
  it currently accepts expression input only. Full module/script compilation
  should get a separate API when statement and module scopes exist.
- The lexer now validates integer literals once and stores the parsed value in
  tokens, so parser/compiler do not duplicate literal parsing.
- `SyntaxError` currently stores only message args. The next parser hardening
  pass should add source location fields and tests before grammar expansion
  becomes large.
- There is still no symbol table because this slice has no names or scopes.
  The first name/load/store feature must add a symbol-table phase instead of
  compiling names directly from AST to bytecode.

Verification:

- `cmake --build build -j2`
- `ctest --test-dir build --output-on-failure`

Result: all tests pass.

## 2026-05-18 Code/Frame Objectization And Slot-Driven Binary Add

Completed:

- Promoted `CodeObject` into the runtime object model:
  - added builtin `code` type
  - added `Runtime::code_type()`
  - added `Runtime::new_code(...)`
  - added code repr/hash/equality/traversal slots
- Promoted `Frame` into the runtime object model:
  - added builtin `frame` type
  - added `Runtime::frame_type()`
  - added `Runtime::new_frame(...)`
  - made VM execution allocate typed frame objects
  - added frame repr and traversal of code + value stack
- Added a first execution-protocol slot:
  - `TypeSlots::binary_add`
  - centralized `object_binary_add(...)`
  - moved `int`/`bool`/`str` `+` behavior out of VM opcode internals
- Changed `BINARY_ADD` to dispatch through the object protocol instead of
  duplicating type checks inside `VirtualMachine`.
- Added bytecode tests covering:
  - runtime-owned code objects
  - custom heap-type `binary_add` slot dispatch through the VM
  - code object traversal of constants
  - frame object typing, repr, stack/code traversal

Design notes:

- `code` and `frame` are worth objectizing early because traceback, debug
  hooks, future function objects, and self-bootstrap reflection need them to be
  typed runtime objects rather than permanently hidden C++ helpers.
- `VirtualMachine`, `InterpreterState`, and `ThreadState` should remain host
  execution infrastructure for now. Exposing them directly to script space this
  early would leak unstable scheduler internals into the object model.
- The heap-type slot inheritance path initially dropped newly added slots. The
  new bytecode test caught that immediately, and `inherit_slots(...)` now
  carries `binary_add` too. This is exactly the class of debt each slot
  addition must review.
- There is still an important follow-up debt: frame allocation currently grows
  runtime heap monotonically because there is no GC or frame freelist yet.
  Before deep recursion, traceback retention, or long-running execution become
  common, GC/root integration for active and dead frames must be designed.
- `binary_add` is only the first arithmetic protocol entry. The next operator
  expansion should be designed as a coherent numeric/sequence protocol family,
  not as one-off opcode patches.

Verification:

- `cmake --build build -j2`
- `ctest --test-dir build --output-on-failure`
- `git diff --check`

Result: all tests pass.

## 2026-05-17 TypeObject Slot Dispatch

Completed:

- Replaced C++ virtual semantic dispatch with CPython-style type slot dispatch.
- Added `TypeSlots` for the current object protocol:
  - `repr`
  - `hash`
  - `equal`
  - `truth`
  - `traverse`
- Moved builtin behavior into explicit slot factories for:
  - `object`
  - `type`
  - `NoneType`
  - `int`
  - `bool`
  - `str`
  - `tuple`
  - `dict`
- Added `Runtime::new_heap_type` so tests and future class support can create
  heap types without editing central object code.
- Added slot inheritance from base type to derived heap type.
- Kept the bootstrap invariant that builtin types are ordinary `TypeObject`
  instances whose behavior is also slot-driven.
- Added tests proving custom slot behavior, slot inheritance, heap type
  `is_instance`, builtin bool/int semantics, dict key merging, Python error
  paths, and child traversal.

Design notes:

- `Object` now owns identity and type pointer only. Python-visible semantics
  belong to `TypeObject`, which avoids baking behavior into C++ subclasses.
- The current MRO implementation is single-inheritance only. Multiple
  inheritance must use a real C3 linearization before user classes are exposed.
- `PythonError` is still a string-typed transitional exception carrier. The
  next hardening step is runtime-owned Python exception objects/types.

Verification:

- `cmake --build build -j2`
- `ctest --test-dir build --output-on-failure`

Result: all tests pass.

## 2026-05-17 Runtime Exception Objects

Completed:

- Added runtime-owned builtin exception type hierarchy:
  - `BaseException`
  - `Exception`
  - `TypeError`
- Added `ExceptionObject` carrying Python exception type and message.
- Changed `PythonError` from a string-typed carrier into a C++ unwinding shell
  around an `ExceptionObject`.
- Added `Runtime::new_exception` and `Runtime::new_type_error` as the current
  exception allocation entry points.
- Added `raise_type_error` so object slots and core helpers do not hand-build
  Python errors.
- Added a runtime pointer to every runtime-allocated `Object`, avoiding global
  exception state while letting slots allocate errors in the owning runtime.
- Updated dict/null-key and unhashable-dict paths to raise object-backed
  `TypeError`.
- Cached `PythonError::what()` in the C++ shell so it remains valid according to
  the `std::exception` contract.

Design notes:

- Python-visible errors now carry a real Python object and type pointer.
- Remaining host-side `std::logic_error` use is reserved for internal API
  misuse where no runtime exists to allocate a Python exception object.
- Future exception work should add args tuples, traceback/context/cause fields,
  and normalized exception matching before VM bytecode exception handling.

Verification:

- `cmake --build build -j2`
- `ctest --test-dir build --output-on-failure`

Result: all tests pass.

## 2026-05-17 Ordered Open-Addressed Dict

Completed:

- Replaced dict linear lookup with an ordered compact-table style layout:
  - `entries_` preserves insertion order.
  - `indices_` provides open-addressed hash lookup.
- Kept object-key semantics through stored hash plus `object_equal`.
- Preserved Python-visible behavior already covered by tests:
  - `True` and `1` merge as the same key.
  - tuple structural keys remain retrievable.
  - updating an existing key does not change size or insertion position.
- Added tests covering:
  - insertion-order preservation.
  - growth across many keys.
  - retrieval after resizing.
  - hash collisions between unequal object keys.

Design notes:

- Deletion/tombstones are intentionally not exposed yet because delete
  semantics are not implemented. When `del` support is added, dict must gain a
  tombstone state and compaction policy instead of removing ordered entries
  directly.
- The probing sequence is CPython-inspired perturb probing, while the external
  behavior remains independent of CPython's private layout.

Verification:

- `cmake --build build -j2`
- `ctest --test-dir build --output-on-failure`

Result: all tests pass.

## 2026-05-17 List Type Foundation

Completed:

- Added builtin `list` type and runtime constructor.
- Added `IndexError` to the runtime exception hierarchy.
- Added `ListObject` with:
  - ordered mutable item storage.
  - append.
  - get/set with negative index normalization.
  - child traversal.
- Added list type slots:
  - repr: `[item, ...]`
  - hash: raises object-backed `TypeError`
  - equality: element-wise sequence equality
  - truth: false only when empty
  - traverse: visits contained objects
- Added tests covering empty/non-empty truth, repr, append, negative indexing,
  structural equality, unhashability, out-of-range `IndexError`, and traversal.

Design notes:

- List is a distinct mutable object type, not a tuple wrapper.
- Index normalization lives in `ListObject`, so future bytecode/builtin paths
  can share the same bounds behavior.
- Slice semantics are intentionally not introduced until the sequence protocol
  is formalized.

Verification:

- `cmake --build build -j2`
- `ctest --test-dir build --output-on-failure`

Result: all tests pass.

## 2026-05-17 GoogleTest Migration and Pre-Commit Review Rule

Completed:

- Replaced the ad hoc C++ test harness with GoogleTest.
- Added `FetchContent`-based GTest integration with pinned `v1.14.0`.
- Split the core object test suite into focused `TEST(...)` cases for:
  - builtin bootstrap.
  - exception hierarchy.
  - bool/int semantics.
  - list semantics.
  - list error paths.
  - dict key behavior.
  - dict growth and collisions.
  - dict error paths.
  - repr and exception objects.
  - heap type slot inheritance.
  - root and child traversal.
- Updated the development guide so every commit must include a technical debt
  review and a `doc/development_status.md` update.
- Removed a `reinterpret_cast` from the CLI entrypoint during the review pass.

Design notes:

- GTest is now the default C++ test layer; it will make future bytecode and
  extension tests easier to isolate.
- The current test suite still covers only the core object layer. Python-level
  and bytecode-focused tests remain the next missing layer.

Verification:

- `cmake -S . -B build`
- `cmake --build build -j2`
- `ctest --test-dir build --output-on-failure`

Result: all tests pass.

## 2026-05-17 Python String and Exception Repr Escaping

Completed:

- Added a single shared escape routine for Python single-quoted display.
- Updated `str` repr to escape:
  - backslash
  - single quote
  - newline
  - tab
  - carriage return
  - other control bytes as `\xNN`
- Updated `ExceptionObject` repr to reuse the same escaping path.
- Added tests covering escaped strings and escaped exception messages.

Design notes:

- String display now follows a Python-like debug repr model rather than raw
  passthrough bytes.
- The same escape behavior should be reused by future traceback and formatting
  code to avoid drift.

Verification:

- `cmake --build build -j2`
- `ctest --test-dir build --output-on-failure`

Result: all tests pass.

## 2026-05-17 Object Layer Source Split

Completed:

- Split the former monolithic `Objects/object.cpp` into focused translation
  units:
  - `object_core.cpp`
  - `object_primitives.cpp`
  - `object_sequences.cpp`
  - `object_mapping.cpp`
  - `object_exception.cpp`
  - `object_utils.cpp`
- Centralized shared object formatting helpers in `object_utils`.
- Kept object semantics unchanged while improving locality of type, sequence,
  mapping, and exception responsibilities.
- Removed the old monolithic source file from the build.

Design notes:

- The object layer is now organized by responsibility instead of by historical
  accretion.
- Runtime.cpp is still a reasonable candidate for a later split when the next
  subsystem boundary becomes clearer.

Verification:

- `cmake --build build -j2`
- `ctest --test-dir build --output-on-failure`

Result: all tests pass.

## 2026-05-17 Runtime Layer Source Split

Completed:

- Split the former monolithic `Objects/runtime.cpp` into focused translation
  units:
  - `runtime_bootstrap.cpp`
  - `runtime_accessors.cpp`
  - `runtime_factories.cpp`
  - `runtime_roots.cpp`
- Kept runtime bootstrap, accessors, object factories, root management, and
  type initialization in separate compilation units.
- Removed the old monolithic runtime source from the build.

Design notes:

- Runtime initialization is now easier to read and extend without turning one
  file into a full subsystem dump.
- The next natural split point is likely the test layer or future module/import
  code, not more churn inside object semantics.

Verification:

- `cmake --build build -j2`
- `ctest --test-dir build --output-on-failure`

Result: all tests pass.

## 2026-05-17 Test Layer Source Split

Completed:

- Split the former monolithic C++ core test file into focused test translation
  units:
  - `test_bootstrap.cpp`
  - `test_sequences.cpp`
  - `test_mapping.cpp`
  - `test_heap_types.cpp`
- Extracted shared test helpers into `test_support.hpp`.
- Kept the same focused behavior coverage while improving test locality and
  maintainability.

Design notes:

- Test files now mirror the object/runtime responsibility boundaries.
- This structure should make future bytecode, module, and extension test
  expansion easier to reason about.

Verification:

- `cmake --build build -j2`
- `ctest --test-dir build --output-on-failure`

Result: all tests pass.

## 2026-05-17 Exception Args Semantics

Completed:

- Added Python-level `args` storage to `ExceptionObject` as a `TupleObject`.
- Kept `message()` as the host-facing exception message used by
  `PythonError::what()`.
- Added a vector-args `Runtime::new_exception` overload.
- Made string-based exception construction create a one-item args tuple.
- Updated exception repr to render from args instead of directly formatting a
  message string.
- Added exception traversal for args so future GC can see the tuple.
- Added tests covering one-arg exceptions, multi-arg exceptions, repr, and
  traversal.

Design notes:

- Exception objects now have a real Python-facing payload shape instead of only
  a C++ message string.
- Future traceback/context/cause work can build on this object layout without
  changing the basic exception constructor contract again.

Verification:

- `cmake --build build -j2`
- `ctest --test-dir build --output-on-failure`

Result: all tests pass.

## 2026-05-17 Minimal Bytecode VM Slice

Completed:

- Added initial bytecode model:
  - `Opcode`
  - `Instruction`
  - `CodeObject`
  - opcode names
  - stack effects
- Added initial VM model:
  - `InterpreterState`
  - `ThreadState`
  - `Frame`
  - `VirtualMachine`
- Added active frame tracking with RAII cleanup during execution.
- Added direct bytecode execution for:
  - `LOAD_CONST`
  - `BINARY_ADD`
  - `RETURN_VALUE`
- Added `BINARY_ADD` support for `int + int` and `str + str`.
- Added Python `TypeError` propagation for unsupported `+` operands.
- Added host `BytecodeError` for malformed bytecode and VM invariant failures.
- Added focused bytecode tests for metadata, stack effects, successful
  execution, Python errors, stack underflow, invalid constants, invalid opcode,
  and active frame cleanup after errors.

Bootstrap notes:

- This is the execution-kernel seed, not the final user entrypoint.
- The long-term route is self-bootstrapping: source -> parser/compiler -> code
  object -> module execution, with builtins and standard library pieces loaded
  by the runtime rather than permanently hand-assembled in C++.
- Hand-written bytecode remains acceptable only for focused VM tests and early
  bootstrap verification.

Verification:

- `cmake --build build -j2`
- `ctest --test-dir build --output-on-failure`

Result: all tests pass.
