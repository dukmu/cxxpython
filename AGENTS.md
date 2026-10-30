# cxxpython Agent Guide

This repository is an independent restart of `cxxpython`: a Python 3 runtime,
compiler, VM, and extension platform implemented in a C/C++ mix.

## Active Architecture

- `cxxpython_foundation_redesign_plan_2.md` is the authoritative implementation
  plan. It supersedes the first plan's serial execution choice. Where older
  documents or this guide disagree, follow the second plan and
  update the conflicting guidance.
- The current implementation is `cxxpython::core`, built as `cxxpython_core`
  with one `cxxpython` executable. Headers live in `Include/cxxpython/`;
  compiler-private IR lives in `Include/internal/`. Use the CPython-shaped
  `Objects/`, `Python/`, `Parser/` and `Programs/` boundaries described in
  `doc/architecture_design.md`. Legacy source, libraries, tests and extension
  APIs have been deleted at the user's request; do not bring them back as a
  fallback or expose the old interpreter's capabilities as current support.
- The first foundation uses boxed values, immutable native layouts, stable
  precise roots, caller-owned result slots, tracing GC without Python object
  reference counting. Multiple OS threads must execute Python bytecode and
  object protocols simultaneously in one Interpreter and one shared heap.
- Follow N0 -> N1 -> N2 -> N3 before expanding the remaining M0-M9 capabilities.
  Use thread-owned root arenas, request/ack/completed GC epochs, rooted shared
  reads, explicit field synchronization and immutable metadata snapshots.
  No interpreter-wide mutex may surround ordinary execution in the final path.
  NativeWait/I/O progress and separate interpreters are not CPU-parallel proof.
- Ordinary namespaces use prepared nodes and short per-object commits so rollback
  deletion does not need managed allocation; read views own precise roots.
  Type/class metadata retains immutable snapshot publication. See Plan 2 section
  0.9; do not replace either contract with an interpreter-wide namespace lock.
- During migration, identify the remaining serialized/unsafe boundaries
  explicitly; do not expose unconverted ObjectOps as concurrently safe.
  A transitional serial guard is not completion of N1-N3 or a hidden final GIL.
- The user-directed repository consolidation overrides earlier legacy-retention
  instructions, not the language/library/concurrency/performance acceptance
  gates. Historical plans live in `doc/archive/`; they are not active contracts.
- Advance through N0-N6 and the complete M0-M9 acceptance gates. Record actual tests,
  remaining gates and the next dependency in `doc/development_status.md`.
  Do not mark a whole milestone complete based on a narrower passing slice.
- The active service goal covers the generated Python 3.12 frontend, unified
  imports and the remaining standard-library/product/N4-N6 work. A verified
  slice does not complete that full objective;
  the full language, standard-library and product scope remains in Plan 2
  sections 0.8 and 11.2. Complete a stage only on its actual acceptance evidence,
  then promptly commit/push and create the next dependent stage goal.
- `Grammar/python.gram` and `Grammar/Tokens` are fixed CPython 3.12.14 inputs.
  Use the vendored pegen grammar reader/left-recursion analysis and the standalone
  C++ output backend in `Tools/peg_generator/`; do not add handwritten grammar
  branches or retain the former parser as a fallback. The generated parser and
  tokenizer depend only on the C++ standard library. Native CST-to-AST lowering
  is distinct from recognition and must report missing execution capabilities
  honestly. Regenerate with `regen-parser` and verify `parser-generated-check`.
- Follow CPython's native/Python responsibilities: `Modules/` supplies necessary
  C/C++ capabilities, `Lib/` supplies Python libraries and policy. Do not rewrite
  an entire Python library in C++ just to avoid completing its syntax/dependencies.
  Fixed CPython source may be adapted with provenance, notices and licenses;
  its RC/GIL/private ABI assumptions do not override this runtime's contracts.
- Prefer copying fixed CPython implementations with provenance over independently
  approximating their behavior. Keep temporary `Lib/` adaptations in the
  compatibility ledger with their missing dependency and removal condition;
  preserve error paths and test them against the fixed reference. Runtime GC,
  parallelism and extension ABI differences do not justify Python-level semantic
  changes. Bootstrap readiness is not completeness of builtins or the library.
- The user allows limited, explained performance regressions, not very large or
  exponential degradation. Preserve raw complexity/performance measurements;
  real CPU parallelism alone does not meet absolute performance acceptance.
- Preserve development history: append new entries or insert them at the top
  of `doc/development_status.md`; do not rewrite earlier entries.
- Generate measurement tables with `Tools/benchmarks/record.py`
  from retained raw runs. Do not hand-copy benchmark rows or derived speedups.
- Do not rerun the full performance suite for every stable commit. Reuse retained
  baselines; measure affected hot paths, new workloads or concrete regressions.
  Correctness-only changes need relevant regression tests, not unrelated CPU probes.
- The user authorizes timely stable-point commits after verification. Commit
  completed vertical slices promptly, excluding unrelated user changes.

## Direction

- Prefer correctness and Python semantics over short-term compatibility hacks.
- Use test-driven development. Every supported behavior needs focused tests and,
  when useful, script-level integration tests.
- Align architecture and naming with CPython where it helps readability:
  `Include/`, `Objects/`, `Python/`, `Parser/`, `Modules/`, `Lib/`,
  `Programs/`, `Tools/`, and `Tests/`.
- Keep internal object layout, exception paths, GC ownership, descriptor
  semantics, import behavior, and bytecode execution explicit and documented.
- Avoid stringly typed runtime state. Prefer typed structs/classes and stable
  internal APIs.
- Do not add partial features behind vague "not implemented" paths unless a
  milestone explicitly marks them experimental and tests lock down the error.

## Engineering Rules

- Read existing design docs in `doc/` before implementing a subsystem.
- Keep reusable dependencies, fixed reference interpreters, sanitizer builds and
  verification logs under ignored repository `.cache/` (ordinary build: `build/`).
  `/tmp` is disposable and may be cleared on restart; never use it as the only
  copy of reusable build state or verification evidence. After a continuation,
  check process handles and artifacts before assuming a build/test survived.
- Use the checked-in `.clang-format` for all C++ sources. Run
  `cmake --build build --target format` after edits and `format-check` before
  committing. CTest also checks formatting. Do not hand-format generated parser,
  grammar headers, Unicode tables or third-party code. `Lib/` is reserved for the interpreter's standard
  library. Host-side Python test drivers belong in `Tests/python/`, native
  contract tests in `Tests/cpp/`, benchmarks in `Tools/benchmarks/`.
- Small vertical slices are preferred: parser/compiler bytecode, runtime
  behavior, and tests should land together.
- New object types must define ownership, traversal, identity, equality, hash,
  repr/str, descriptor behavior if relevant, and tests.
- New VM opcodes must define stack effect, exceptions, bytecode encoding,
  disassembly, and tests.
- C/C++ extension APIs must be treated as public ABI/API surfaces; document them
  before expanding them.
- Keep CPython compatibility notes close to the code and tests.

## Quality Bar

- No silent fallback from Python exceptions to C++ runtime errors.
- No hidden global mutable state without lifecycle, thread-safety, and tests.
- No unrooted GC-managed object may be live across code that can allocate or
  collect.
- No class, function, module, or helper should exist only to patch over unclear
  ownership or unclear semantics.

