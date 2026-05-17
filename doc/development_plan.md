# 开发计划

## Milestone 0: 工程骨架

目标：

- 初始化 CPython-like 目录。
- CMake 或 Meson 构建。
- 单元测试框架、脚本测试框架、CI 入口。
- 最小 CLI: `cxxpython --version`。

验收：

- 空 VM 可启动。
- `ctest` 或等价命令全绿。

## Milestone 1: 基础对象和类型系统

目标：

- `object`, `type`, `NoneType`, `bool`, `int`, `str`, `tuple`, `list`, `dict`。
- `bool` 是 `int` 子类。
- 通用 object-key dict。
- 统一 hash/equality/rich compare。
- Python exception object 基础体系。

验收：

- literal object tests。
- dict key 合并测试：`True` 与 `1`。
- MRO and `isinstance`/`issubclass` tests。

## Milestone 2: 字节码指令集

目标：

- 定义 bytecode format、opcode metadata、stack effect。
- constants/names/varnames/code object。
- disassembler。
- 基础算术、load/store、jump、call、return、subscript。

验收：

- direct bytecode tests。
- stack underflow and opcode error tests。

## Milestone 3: VM 和执行帧

目标：

- `InterpreterState`, `ThreadState`, `Frame`。
- traceback line table。
- function call protocol。
- GC roots include active frames.
- unified exception propagation。

验收：

- nested calls。
- exception traceback。
- manual GC during active frame execution。

## Milestone 4: Parser and compiler subset

目标：

- lexer/parser/AST。
- symbol table。
- compile expressions, statements, functions, control flow。
- script runner。

验收：

- arithmetic/control/function scripts。
- syntax error type and location tests。

## Milestone 5: 类系统

目标：

- class creation。
- attribute lookup。
- descriptor protocol。
- `property`, `staticmethod`, `classmethod`, `super`。
- inheritance and C3 MRO。

验收：

- property class/instance behavior。
- data descriptor priority。
- method binding。
- metaclass conflict tests。

## Milestone 6: C/C++ 模块支持

目标：

- native module definition API。
- native function binding。
- type creation from C/C++。
- argument parser。
- exception setting。
- RAII root handles。

验收：

- sample C extension。
- sample C++ extension。
- error cleanup and repeated import tests。

## Milestone 7: Python 模块和 import

目标：

- `sys.modules`, module spec, package, relative import。
- filesystem importer。
- pure Python `Lib/` loading。
- bootstrap importlib subset。

验收：

- package/submodule tests。
- circular import tests。
- failed import cleanup tests。

## Milestone 8: 基础 Python 模块

目标：

- `builtins`, `sys`, `os`, `posix`, `gc`, `_io`, `io`, `_re`, `re`, `traceback`, `types`, `collections` subset。

验收：

- module-level compatibility scripts。
- stdout/stderr/file IO tests。
- regex wrapper tests。

## Milestone 9: 基础 CLI

目标：

- `cxxpython file.py`。
- `-c`, `-m`, REPL。
- path/env handling。
- exit code semantics。

验收：

- CLI integration tests。
- subprocess tests。

## Milestone 10: 协程和生成器增强

目标：

- generator send/throw/close。
- `yield from`。
- coroutine object。
- `await` protocol。

验收：

- generator lifecycle tests。
- coroutine protocol tests。

## Milestone 11: 真多线程支持

目标：

- thread state per OS thread。
- runtime lock strategy。
- GC synchronization。
- native extension thread boundary。

验收：

- stress tests。
- parallel module import tests。
- GC while threads execute。

## Milestone 12: 异步支持

目标：

- event loop primitives。
- task/future subset。
- async iterator/context manager。
- `asyncio` subset direction。

验收：

- async/await scripts。
- cancellation/error propagation tests。

## Milestone 13: POSIX 支持

目标：

- `posix` module。
- filesystem, env, process, errno。
- platform-specific capability guards。

验收：

- POSIX behavior tests on Linux。
- error mapping to `OSError` subclasses。

## Milestone 14: 底层 Python/C/C++ 库和完整解析器

目标：

- expand CPython C API subset。
- expand C++ embedding API。
- full Python grammar。
- larger `Lib/` subset。
- performance passes: compact dict/list, interned strings, opcode specialization。

验收：

- CPython comparison suite subset。
- extension compatibility samples。
- long-running stress tests。

