# Development Status

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

- Add tuple and dict literal syntax plus `BUILD_TUPLE`/`BUILD_MAP` or equivalent
  bytecode so Python golden tests can cover `type('A', (object,), {...})`.
- Then add base-list parsing for `class A(Base):` and route it through the same
  bases-aware class creation API.

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
