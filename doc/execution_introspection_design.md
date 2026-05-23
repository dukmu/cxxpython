# Execution Introspection, Debugging, And Concurrency Design

This document records the next substrate direction for traceback, lexer/parser
exposure, `dis`, `pdb`, and concurrency.  The goal is CPython-shaped layering:
native code owns irreducible runtime state and host syscalls, while script
modules own public policy.

## Traceback And Debug Surface

Current state:

- `CodeObject` exposes basic metadata (`co_name`, `co_filename`, arg counts,
  names, varnames, constants, stack size, first line, and `co_lines()`).
- `CodeObject._cxx_instructions()` exposes a cxxpython-private instruction
  stream for script-level `dis`.
- `Frame` exposes `f_code`, `f_locals`, `f_globals`, `f_builtins`,
  `f_lasti`, and `f_lineno`.
- `ThreadState` owns a handled-exception stack used by `sys.exc_info()`.
- `BaseException.__traceback__` returns a real traceback object chain or
  `None`.
- Traceback objects expose `tb_next`, `tb_frame`, `tb_lasti`, and
  `tb_lineno`.
- `Lib/traceback.py` formats traceback objects and caller-provided filtered
  lists.  This keeps formatting policy out of the CLI and REPL.
- `ThreadState` stores a global trace function exposed through
  `sys.settrace()` / `sys.gettrace()`.
- VM safe points emit `call`, `line`, `return`, and `exception` trace events
  with recursion suppression while trace callbacks run.
- `Lib/linecache.py` provides script-level source-line caching for traceback
  and future debugger display.  Freshness checks are conservative until
  `posix.stat` is exposed.

Target shape:

- Add frame-level trace controls (`f_trace`, `f_trace_lines`,
  `f_trace_opcodes`) and profile hooks.
- Add exception chaining fields (`__context__`, `__cause__`, and
  `__suppress_context__`) and writable traceback assignment.
- Keep `traceback.py` as the public formatter over the object model.
- Keep `sys.exc_info()` backed by thread-state.  Its traceback element is now
  the handled exception's traceback object.
- Preserve line-table data in `CodeObject` so `pdb`, traceback, and `dis`
  share the same instruction-to-source mapping.
- Keep source loading in `linecache.py`; native code should expose only real
  filesystem/import-loader facts such as stat metadata and loader source.
- `co_lines()` currently reports instruction-index ranges rather than CPython
  byte offsets because cxxpython bytecode is not yet a CPython-compatible
  byte stream.  Keep this explicitly documented until a public bytecode format
  decision is made.

## Lexer, Parser, And Script Exposure

Current state:

- The native compiler has a real lexer/parser and bytecode builder.
- `_tokenize.TokenizerIter(source)` now exposes tokenizer records using the
  CPython private-module entrypoint name instead of an invented helper.
- `Lib/token.py` defines CPython token constants for the supported tokenizer
  surface.
- `Lib/tokenize.py` is the script facade and returns `TokenInfo` records whose
  `type` values are numeric token constants.  Operators are reported as
  `OP`; `TokenInfo.exact_type` returns the exact operator token.

Target shape:

- Expand `Lib/tokenize.py` toward CPython's full bytes/readline, encoding,
  comments, `NL`, and `untokenize` behavior only as the underlying `bytes`,
  encoding, and regex/string infrastructure becomes real.
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

- Expose bytecode instructions as script-visible records only after deciding
  whether this VM's bytecode is a CPython-compatible public format or a
  cxxpython-private code-object API.
- Preserve opcode names, operands, stack effects, and source line mapping.
- Keep formatting and command-line behavior in `Lib/dis.py`; native code only
  supplies facts.
- Do not expose `_opcode` as a thin wrapper over the internal VM enum unless
  its numeric opcode table is intentionally aligned with CPython.  CPython has
  `_opcode`, but a same-name module with incompatible opcode numbers is worse
  than an absent module.
- `code.co_lines()` is now the first public line-table API for this path; the
  next acceptable step is a clearly named instruction iterator on code objects
  or a CPython-compatible opcode table, not an `_opcode` module over private
  enum values.
- `Lib/dis.py` now consumes `code._cxx_instructions()`.  Its opcode numbers
  and offsets are explicitly cxxpython-private and must not be exported
  through CPython's `_opcode` compatibility surface.
- `Lib/dis.py` resolves `argval`/`argrepr` for the currently exposed operand
  families and marks jump targets in script code.

Acceptance tests:

- `dis.get_instructions(func)` yields instruction objects in bytecode order.
- Each instruction exposes `opname`, `opcode`, `arg`, `argval`, `argrepr`,
  `offset`, `starts_line`, and `is_jump_target`.
- `dis.dis(func)` prints through script code and matches the supported subset.

## `pdb` Preparation

Current substrate before `Lib/pdb.py`:

- Real traceback object chain is present.
- Frame locals/globals/builtins are visible.
- A tracing hook in thread-state, shaped like `sys.settrace()`, is present
  for `call`, `line`, `return`, and `exception` events.
- Breakpoint state and stepping policy in script code.

Native code should only call trace hooks at VM safe points.  Breakpoint
commands, source display, and interaction policy belong in scripts.

## Threading Model

Initial model:

- Introduce `InterpreterState` and `ThreadState`.
- Keep active frame, recursion depth, and current handled exception in
  `ThreadState`; add trace function and pending async events there when those
  features arrive.
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
