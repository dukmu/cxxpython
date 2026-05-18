# Development Status

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
