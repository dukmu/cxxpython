# Python 特性支持目标

状态标记：

- P0: 第一阶段必须可用。
- P1: 主要 Python 3 兼容能力。
- P2: 完整度、性能、生态兼容增强。

## 语言核心

| 特性 | 目标 |
| --- | --- |
| `None`, `bool`, `int`, `str`, `bytes` | P0 |
| `tuple`, `list`, `dict`, `set`, `frozenset` | P0/P1 |
| 切片、负索引、迭代协议 | P0 |
| 函数、默认参数、`*args`, `**kwargs` | P0 |
| 闭包、`nonlocal`, `global` | P1 |
| 生成器、`yield from` | P1 |
| 异常、traceback、chaining | P0/P1 |
| `ExceptionGroup`, `except*` | P2 |
| class、继承、MRO、descriptor、property | P0/P1 |
| metaclass、`super()`、classmethod、staticmethod | P1 |
| context manager、`with` | P1 |
| pattern matching | P2 |
| annotations、typing runtime hooks | P2 |

## 编译器和语法

| 特性 | 目标 |
| --- | --- |
| lexer/parser/AST | P0 |
| 表达式、语句、控制流 | P0 |
| 函数和类定义 | P0 |
| comprehensions | P1 |
| f-string | P1 |
| async/await syntax | P1/P2 |
| 完整 Python 3 grammar 对齐 | P2 |
| `.pyc`/marshal/code object 兼容方向 | P2 |

## VM 和运行时

| 特性 | 目标 |
| --- | --- |
| stack bytecode VM | P0 |
| code object / frame / thread state | P0 |
| traceback and line table | P0/P1 |
| GC traversal and roots | P0 |
| import system | P0/P1 |
| descriptor and attribute lookup | P0 |
| generators | P1 |
| coroutines | P1 |
| async event loop primitives | P2 |
| true multithreading runtime model | P2 |
| debug/profiling hooks | P2 |

## 模块和生态

| 特性 | 目标 |
| --- | --- |
| builtin module | P0 |
| `sys`, `os`, `gc`, `_io`, `_re`, `traceback` | P0/P1 |
| pure Python `Lib/` subset | P1 |
| C module ABI | P1 |
| C++ binding API | P1 |
| POSIX integration | P1/P2 |
| packaging/importlib compatibility | P2 |
| CPython C API compatibility subset | P2 |

## CLI 和工具

| 特性 | 目标 |
| --- | --- |
| `cxxpython file.py` | P0 |
| REPL | P0/P1 |
| `-c`, `-m`, `PYTHONPATH` equivalent | P1 |
| disassembler | P1 |
| test runner integration | P1 |
| debug flags, GC stats, trace flags | P1/P2 |

