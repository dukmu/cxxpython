# 开发指南

## 工作方式

采用测试驱动开发：

1. 写 feature note 或更新特性矩阵。
2. 写 C++ focused test。
3. 写 Python script integration test。
4. 实现最小正确 vertical slice。
5. 跑 focused tests。
6. 跑全量 tests。
7. 按本指南做提交前技术债审查。
8. 更新文档、兼容性说明和 `doc/development_status.md`。

## 代码质量原则

- 不为通过当前测试牺牲 Python 语义。
- 不使用 string prefix 解析异常类型。
- 不把 typed runtime state 放进用户可见 dict。
- 不复制核心逻辑到 opcode、builtin、type method 三处。
- 不创建没有所有权模型的裸指针长期对象。
- 不新增无测试的 public API。
- 不为尚未实现的系统能力写 placeholder 行为。例如 POSIX、IO、socket、
  time 等 native hook 必须接真实 substrate 或明确抛 Python 异常，不能返回
  空列表、空字符串、固定成功值来通过测试。
- Public stdlib 优先写在 `Lib/`。C++ native module 默认只能是私有 hook
  或 bootstrap seed；如果新增 public native API，必须在
  `doc/development_status.md` 说明为什么它无法脚本化。
- 每次提交前审查一遍可能造成技术债的设计或者代码，并把结果写入
  `doc/development_status.md`。

## 对象类型实现清单

每个新类型必须回答：

- identity 是什么？
- truthiness 是什么？
- hashable 吗？
- equality/order 如何实现？
- repr/str 是什么？
- 是否支持 instance dict？
- 是否是 descriptor？
- GC 如何 visit refs？
- 构造路径是 type call、literal、opcode，还是 native API？
- 错误类型与 CPython 是否一致？

## VM opcode 实现清单

每个 opcode 必须记录：

- opcode name and numeric id
- operand encoding
- stack input/output
- fast path and generic path
- possible Python exceptions
- line/trace behavior
- focused bytecode test
- compiler integration test

## C/C++ 扩展开发规则

- 所有 extension 返回 Python object handle。
- 所有参数解析失败设置并抛 Python `TypeError`。
- native function 不允许返回 null；除非同时设置异常并走统一异常路径。
- 提供 RAII root guard，保护跨分配和跨 GC 的临时对象。
- 模块初始化必须可重复失败清理，不污染 `sys.modules`。

## 测试分层

```text
Tests/cpp/          C++ unit tests for object and VM internals
Tests/bytecode/     direct bytecode behavior tests
Tests/python/       Python-level golden behavior tests
Tests/compat/       CPython comparison snippets
Tests/extensions/   C/C++ extension ABI tests
```

推荐先建立 golden behavior snippets：同一段 Python 在 CPython 和 cxxpython 上比较 stdout、exception type、repr、MRO、dict behavior。

`Tests/python/` 规则：

- 每个测试用例是一个 `.py` 文件。
- 必须提供同名 `.py.out`，内容是 CLI 合并 stdout/stderr 后的精确输出。
- 期望退出码默认为 `0`；非零退出码用同名 `.py.exit` 指定。
- 新增脚本测试时优先覆盖用户可见语义，不在 C++ test 中重复手写同一段脚本包装。
