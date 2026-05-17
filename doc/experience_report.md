# 经验报告

## 总结

上一版证明了一个小型 Python 运行时可以快速跑通：基础对象、字节码 VM、函数调用、类、描述器、import、部分标准库和 C++ 绑定都能形成闭环。但它也暴露出典型问题：类型系统、异常系统、GC 根、dict 语义、模块状态、字节码和内建函数之间如果没有统一设计，很快会出现重复逻辑和脆弱补丁。

新仓库应从头设计，不沿用脏实现。可以参考旧项目的测试用例和行为经验，但核心结构要重新建立。

## 关键教训

1. `dict` 必须从第一天支持通用 hashable object key。
   string-key map 只能临时服务模块命名空间，不能代表 Python dict。`True` 与 `1`、tuple key、字符串 key、对象 identity hash 都必须在统一 hash/equality API 下运行。

2. `bool` 必须是 `int` 子类。
   `bool.__mro__ == (bool, int, object)`，`True == 1`，`hash(True) == hash(1)`，dict key 合并都属于基础语义，不是边角。

3. 描述器协议必须先设计清楚。
   属性查找顺序必须是 data descriptor、instance dict、non-data descriptor/class attr、`__getattr__`。`property` 在类访问时返回自身，在实例访问时调用 getter；只读 property 赋值抛 `AttributeError`。

4. 异常系统不能混用 C++ `std::runtime_error` 和 Python 异常。
   C++ 异常可以作为宿主实现机制，但运行时对外必须始终抛/传递 Python exception object。兼容桥只能用于迁移期，不能成为架构入口。

5. GC 必须把活动执行帧本身作为根。
   仅标记 frame children 不够。如果 heap frame 没被标记，`gc.collect()` 可以释放正在执行的 frame，造成内存破坏。

6. 运行时内部状态不要藏在 `dynamic_dict` 字符串键里。
   例如 regex match state 应该是 typed object fields，而不是 `_matched`、`_start` 这类用户可污染属性。

7. `Function::call`、opcode helper、type native method、builtin helper 必须有清晰分层。
   同一语义在多个位置重复实现会导致比较、调用、异常、绑定行为逐渐分叉。

8. 测试必须覆盖 C++ API、VM 字节码、脚本级行为和错误路径。
   只测 happy path 不够。类型错误、属性错误、import error、GC、descriptor、dict key 合并、traceback 都要有 focused tests。

9. 不要让 TODO 表示用户可触达行为。
   如果功能未实现，要有明确异常类型和测试；如果功能是核心 Python 语义，应排入里程碑，而不是长期停在 TODO。

## 可复用经验

- 类型对象、对象实例、描述器、函数、模块、异常、frame、traceback 都应该是 GC-managed object。
- 模块 namespace 可以使用 dict，但模块对象本身需要 typed fields。
- 标准库可以先用 Python 子集实现，再逐步替换性能敏感部分为 C/C++ 模块。
- 字节码 VM 比 AST interpreter 更适合扩展到调试、traceback、生成器、协程、JIT/优化。
- C/C++ 扩展 API 要早期定义最小稳定核心，否则后期会被内部对象布局绑死。

