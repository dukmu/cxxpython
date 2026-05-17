# 架构设计

## 目录结构

尽量对齐 CPython，但允许 C++ 风格实现：

```text
Include/        public and internal headers
Objects/        object implementations: int, str, dict, type, function, frame
Python/         compiler, bytecode, VM, runtime state, import core
Parser/         lexer, parser, AST builder
Modules/        builtin C/C++ modules
Lib/            Python standard library subset
Programs/       CLI, REPL, embedding entrypoints
Tools/          generators, disassembler, dev utilities
Tests/          C++ unit tests and Python script tests
doc/            design docs and milestone plans
```

## 核心分层

1. Object model
   所有 Python 值继承统一 `PyObject`/`Object` 基类，包含 type pointer、GC header、optional instance dict。基础类型使用 typed fields，不使用 stringly dynamic state。

2. Type system
   `type` 自举必须严谨。每个 `PyTypeObject` 包含 name、module、base/bases、MRO、slots、dict、flags。类型初始化分 allocation phase 和 ready phase，避免循环依赖。

3. Attribute and descriptor engine
   属性查找由一个中心 API 实现。所有 VM opcode、builtin、C API 都走同一路径，避免重复。

4. Exception runtime
   Python exception object 是唯一运行时错误载体。C++ throw 只用于控制流传递，不能以 `std::runtime_error("TypeError: ...")` 表示 Python 错误。

5. Dict and hash
   dict 使用 object key、stored hash、insertion order、open addressing 或 compact table。hash/equality 统一走 `PyObject_Hash` 与 `PyObject_RichCompareBool`。

6. Compiler pipeline
   Source -> tokens -> AST -> symbol table -> bytecode -> code object。早期可以只实现子集，但 pipeline 不应临时绕过 symbol table。

7. VM
   Stack VM with explicit opcode metadata: name, stack effect, operand format, exception behavior. `Frame`, `ThreadState`, `InterpreterState` 从第一天存在。

8. GC
   采用 tracing GC 起步，可叠加引用计数或 arena。所有可达根必须明确：builtins、modules、active frames、C stack handles、intern tables、static singletons。

9. Modules
   Builtin modules 是 typed native modules。Python modules 使用 dict namespace。Import system 逐步对齐 importlib，但核心 bootstrap 需要最小、可测试。

10. C/C++ extension layer
   先定义稳定最小 API：object handles、type creation、module definition、argument parsing、exception setting、reference/root guard。C++ binding API 构建在 C API 之上。

## C 与 C++ 的分工

- C ABI/API: extension compatibility, stable handles, embedding boundary.
- C++ core: containers, RAII guards, VM internals, parser/compiler implementation.
- 禁止 extension 直接依赖内部 C++ class layout，除非明确标为 unstable internal API。

## 对齐 CPython 的原则

- 语义对齐优先于源码结构照抄。
- bytecode 可先自定义，但 opcode 文档必须完整；后期再评估 CPython bytecode 兼容。
- C API 先支持高价值子集，不承诺一开始完整兼容。
- 标准库优先 pure Python 子集，性能敏感模块再 native 化。

