# Execution Introspection, Debugging, And Concurrency Design

This document records the next substrate direction for traceback, lexer/parser
exposure, `dis`, `pdb`, and concurrency.  The goal is CPython-shaped layering:
native code owns irreducible runtime state and host syscalls, while script
modules own public policy.

## Traceback And Debug Surface

Current state:

- `CodeObject` exposes basic metadata (`co_name`, `co_filename`, arg counts,
  names, varnames, constants, stack size, and first line).
- `Frame` exposes `f_code`, `f_locals`, `f_globals`, `f_builtins`,
  `f_lasti`, and `f_lineno`.
- `BaseException.__traceback__` is a script-visible list of
  `(filename, line, name)` tuples.
- `Lib/traceback.py` formats that seed.  This keeps formatting policy out of
  the CLI and REPL, but it is not the final object model.

Target shape:

- Add a real `traceback` object type with `tb_next`, `tb_frame`,
  `tb_lasti`, and `tb_lineno`.
- Attach traceback objects while exceptions propagate through VM frames.
- Keep `traceback.py` as the public formatter over the object model.
- Add `sys.exc_info()` only after exception state is represented in
  thread-state, not as process-global ad hoc storage.
- Preserve line-table data in `CodeObject` so `pdb`, traceback, and `dis`
  share the same instruction-to-source mapping.

## Lexer, Parser, And Script Exposure

Current state:

- The native compiler has a real lexer/parser and bytecode builder.
- `_tokenize.tokens(source, filename)` now exposes the compiler token stream
  as real data from the native tokenizer.
- `Lib/tokenize.py` is the script facade and returns `TokenInfo` records.

Target shape:

- Add `Lib/token.py` constants and map tokenizer kinds to stable token numbers
  once the public token set is ready.
- Add an `_ast` substrate only when AST nodes are real script-visible objects,
  not dictionaries or lossy tuples.
- Add `Lib/ast.py` as the public parser API over `_ast.parse()`.
- Keep parser errors as `SyntaxError` objects with filename, line, offset,
  and text so `traceback`, REPL incomplete-input logic, and editor tooling do
  not string-match messages.

Rules:

- `dis`, `pdb`, and `inspect` must consume public `CodeObject`, `Frame`,
  traceback, token, and AST APIs.  They must not depend on private parser
  structs or C++ visitor classes.
- Native parser APIs must return stable object-space data.  If a structure is
  not stable enough to expose, keep it private and document the missing API.

## `dis` Preparation

Required substrate before `Lib/dis.py`:

- Expose bytecode instructions as script-visible records through a small
  native `_opcode` or code-object method.
- Preserve opcode names, operands, stack effects, and source line mapping.
- Keep formatting and command-line behavior in `Lib/dis.py`; native code only
  supplies facts.

Acceptance tests:

- `dis.get_instructions(func)` yields instruction objects in bytecode order.
- Each instruction exposes `opname`, `opcode`, `arg`, `offset`, and
  `starts_line`.
- `dis.dis(func)` prints through script code and matches the supported subset.

## `pdb` Preparation

Required substrate before `Lib/pdb.py`:

- Real traceback object chain.
- Frame locals/globals/builtins already visible.
- A tracing hook in thread-state, shaped like `sys.settrace()`.
- Breakpoint state and stepping policy in script code.

Native code should only call trace hooks at VM safe points.  Breakpoint
commands, source display, and interaction policy belong in scripts.

## Threading Model

Initial model:

- Introduce `InterpreterState` and `ThreadState`.
- Move active frame, recursion depth, current exception, trace function, and
  pending async events into `ThreadState`.
- Keep a process-level GIL at first so object layout and GC remain correct.
- Route signals to the main interpreter/thread state, matching CPython's
  high-level behavior.

Future true parallelism:

- Do not promise GIL-free execution until object allocation, GC roots, module
  state, extension APIs, and ref/handle policy are designed for it.
- Multiple interpreters can arrive before true shared-heap parallelism if
  module state and builtins are interpreter-local.

## Coroutine And Generator Model

Required object substrate:

- `generator` objects own a suspended frame and resume through the VM.
- `yield`, `yield from`, `await`, and `async def` lower to frame suspension,
  not callback-specific VM shortcuts.
- Coroutine objects expose `send`, `throw`, and `close` consistently with
  generator protocol.

Script policy:

- `asyncio`-like scheduling belongs in `Lib/`.
- Native code only provides frame suspension, await protocol hooks, and a
  minimal selector/event primitive when POSIX support reaches file descriptors.

## POSIX Process Model

Native `posix` should grow real host primitives:

- `fork`, `execve`, `waitpid`, `_exit`
- `open`, `close`, `read`, `write`, `pipe`, `dup`, `dup2`
- `stat`, `fstat`, `lstat`, permission constants, and errno mapping
- environment access/mutation backed by the process environment

Script modules should then build policy:

- `os.py` maps POSIX names and provides portable wrappers.
- `subprocess.py` owns process-launch policy.
- `multiprocessing.py` should be script-first and build on `fork`/`spawn`
  substrate rather than native shortcuts.

No fake process objects should be exposed.  If a syscall is missing, leave the
public API absent or raise a precise exception from the real failing syscall.
