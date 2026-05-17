# cxxpython Agent Guide

This repository is a clean-room restart of `cxxpython`: a Python 3 runtime,
compiler, VM, and extension platform implemented in a C/C++ mix.

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

