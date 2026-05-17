# Development Status

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
