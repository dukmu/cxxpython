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

## 自举原则

- C++ bootstrap 只负责建立不可避免的种子对象：`object`、`type`、核心 slots、基础异常、最小 VM 和 import 前置环境。
- 一旦 VM、code object、module object 和 import core 可用，后续 builtins、标准库子集、测试脚本和开发工具应尽量通过运行时自身加载和执行。
- 手写 bytecode tests 是早期锁定 VM 语义的工具，不是长期执行入口。长期入口必须走 parser/compiler/code object/module 执行链。
- 每个新的运行时能力都要回答：它是 bootstrap seed，还是应该由 Python 层自举出来。如果是 seed，必须保持小而稳定。

## C 与 C++ 的分工

- C ABI/API: extension compatibility, stable handles, embedding boundary.
- C++ core: containers, RAII guards, VM internals, parser/compiler implementation.
- 禁止 extension 直接依赖内部 C++ class layout，除非明确标为 unstable internal API。

## 对齐 CPython 的原则

- 语义对齐优先于源码结构照抄。
- bytecode 可先自定义，但 opcode 文档必须完整；后期再评估 CPython bytecode 兼容。
- C API 先支持高价值子集，不承诺一开始完整兼容。
- 标准库优先 pure Python 子集，性能敏感模块再 native 化。

## 执行对象暴露边界

- `code` 和 `frame` 必须是运行时对象，而不是永久停留在 C++ helper
  层。原因是函数对象、traceback、调试钩子、`sys._getframe`、异常栈和
  自举后的脚本反射都会依赖它们的 Python 身份、类型、repr 和 GC
  traversal。
- `VirtualMachine`、`InterpreterState`、`ThreadState` 当前保持为宿主执行
  基础设施，不直接作为普通脚本对象暴露。未来脚本层需要观察它们时，应
  通过 `sys`、`threading`、trace/profile API 或专门 runtime facade，而不是
  把内部调度器裸露进对象空间。
- VM opcode 不直接复制类型语义。算术、比较、调用、属性访问、下标访问
  都应走对象协议 slot 或统一 runtime API，再由类型实现具体行为。

## Slot 扩展路线

当前 slot 覆盖第一批对象协议：`repr`、`hash`、`equal`、`rich_compare`、`truth`、
`traverse`、`binary_op`、`reflected_binary_op`、`inplace_binary_op`、`call`、
`length`、`getitem`、`setitem`、`contains`、`descr_get`、`descr_set`。后续扩展顺序：

- Numeric: C++ 层使用 `BinaryOp` 统一表示 add/sub/mul/div/mod/pow，并通过
  `object_binary_op` 调度。VM 层应使用携带 `BinaryOp` operand 的通用
  `BINARY_OP` 形态，parser/compiler 层再按优先级把 `+`、`-`、`*` 等语法降级
  到同一协议。`NotImplemented`、subclass priority、reflected/in-place fallback
  需要作为完整协议一次实现，不能散落在各个内置类型中。
- Rich compare: `==`/`!=` 已走 `object_rich_compare`；`lt/le/gt/ge`、`NotImplemented`
  和 reverse/subclass dispatch 需要在控制流前继续补齐。
- Sequence/mapping: length, getitem, setitem, contains, iterator。
- Attribute/descriptor: getattro/setattro, descr_get/descr_set。
- Call/construction: call, new, init。
- Buffer/extension hooks: buffer protocol 和 C/C++ extension 需要的稳定边界。

## VM、栈和脚本层开放性路线

- `CodeObject` 是脚本执行的稳定载体，必须逐步补齐 CPython 类似的元数据：
  `name`、`filename`、line table、constants、names、varnames、freevars、
  cellvars、flags。任何 CLI、traceback、function object、debug API 都不应
  绕过 `CodeObject` 私下保存这些执行信息。
- `Frame` 是运行时可观察对象，不是临时 C++ stack helper。Frame 必须明确
  持有 code、locals、globals、builtins、value stack、block stack、last
  instruction/line。当前 VM 可以保留宿主执行器，但脚本层观察执行状态时
  应通过 frame/code/module 对象，而不是暴露 `VirtualMachine` 内部结构。
- Value stack 只保存 `Object*`，所有跨 GC/分配边界的活跃对象必须能从
  active frames traversal 访问到。后续引入 GC 时，active frame stack、
  block stack、exception state 都必须成为根。
- Name resolution 必须保持一条统一路径：locals -> globals -> builtins。
  opcode、builtin helper、module execution 不能各自实现一套查找。
- VM opcode 只负责调度和栈效果，类型语义由 object/type slots 或统一 runtime
  API 完成。新增 opcode 必须同时更新 stack effect、disassembly/metadata、
  compiler emission、VM execution 和测试。
- Arithmetic opcode must dispatch through `object_binary_op`. The legacy
  `object_binary_add` helper is only a compatibility wrapper for the initial
  `BINARY_ADD` path and must not become the model for adding each operator.
- `COMPARE_OP` 的职责是弹出左右操作数，解码比较 operator，调用
  `object_rich_compare`，再把 Python bool/object 结果压栈。比较语义不能在
  VM 中直接调用 `object_equal` 或识别 `int`/`str` 等具体类型。
- Jump opcodes are structural VM control flow. `JUMP_ABSOLUTE` rewrites the
  frame instruction pointer after validating the target; conditional jumps pop
  one Python object and dispatch truth through `is_true`/type truth slots.
  Parser/compiler control-flow syntax must lower to these opcodes instead of
  embedding truth or branch behavior outside the VM.
- `CALL` 的职责是栈调度：弹出 positional arguments 和 callable，调用统一
  `object_call`/type call slot，再把返回值压栈。函数对象、native function、
  type construction、bound method 后续都必须接入这条协议，而不是让 opcode
  识别具体对象类型。
- `LOAD_ATTR`/`STORE_ATTR` 的职责也是栈调度和 name operand 解码；属性语义
  必须继续由 `object_get_attr`/`object_set_attr` 处理。不要在 VM opcode 中
  复制 descriptor、module dict、instance dict 或 type MRO 规则。
- Future subscript opcodes must follow the same rule: `BINARY_SUBSCR`,
  `STORE_SUBSCR`, containment, and `len()` dispatch through
  `object_get_item`/`object_set_item`/`object_contains`/`object_length`, not
  direct `list`/`dict` checks in the VM.

## 编译器 AST 和赋值目标路线

- Parser 可以先按表达式语法解析左侧，再把表达式显式转换为
  `AssignTarget`。合法 target 和普通表达式必须在 AST 中分离，避免每新增
  一种赋值形式就新增一个 statement 节点或 VM 特判。
- 当前 assignment target family 包括 `NameTarget`、`AttributeTarget` 和
  `SubscriptTarget`。后续 destructuring、slice target、starred target、augmented
  assignment 都应扩展这条 target 层，而不是绕回 parser 分支。
- Symbol table 和 bytecode builder 都通过 `TargetVisitor` 处理赋值目标。目标
  子表达式、目标名称和值表达式的收集/发射顺序必须由这条 visitor 明确表达。
- Compound statements such as `if` own nested statement vectors in AST. Parser
  block handling must use explicit `INDENT`/`DEDENT` tokens and return whether a
  statement is compound instead of relying on hidden parser state.
- Store bytecode 的栈契约要保持集中记录并由测试锁住。当前 `STORE_ATTR`
  使用 object + value；`STORE_SUBSCR` 使用 object + key + value。未来若对齐
  CPython 调整求值顺序，必须同时更新 compiler、VM、stack effect 和测试。
- Subscript expression 是普通表达式，编译为 target object、key expression、
  `BINARY_SUBSCR`。Subscript assignment 使用同一个语法节点转换出的
  `SubscriptTarget`，编译为 target object、key expression、value expression、
  `STORE_SUBSCR`。

## 模块系统路线

- 模块是 typed runtime object，模块 dict 是脚本层可见 namespace。CLI、
  import、`sys.modules`、package execution 都应围绕 module object 工作，
  不再退回裸 dict 模块模型。
- 模块初始化最小字段包括 `__name__`、`__file__`、`__builtins__`。后续导入
  系统加入 `__spec__`、`__package__`、`__loader__`、`__path__`。
- Builtins 应首先作为模块对象存在，再由 frame name resolution 的 builtins
  dict 暴露给脚本。不要在 VM 或 CLI 特判 `print`、`len` 等名字。
- 最小 native builtins 可以作为 bootstrap seed 存在，但必须是普通 typed
  objects，放入 `builtins` module dict，并通过 `LOAD_NAME`/`CALL` 访问。标准
  行为扩展时优先补通用协议，例如 `str()`、keyword arguments、stdout/file
  对象，而不是继续扩展 `print` 的内部特例。
- Import core 引入前，CLI file execution 可以创建 `__main__` module，但一旦
  `sys.modules` 存在，`__main__` 必须通过同一模块注册/清理路径。

## 类型系统开放性路线

- TypeObject 当前只有基础 slots 和单继承 MRO。用户类开放前必须补齐：
  attribute lookup、descriptor protocol、call/new/init、C3 MRO、
  metaclass conflict checks。
- TypeObject 必须持有 runtime dict。内建类型和 heap type 的 `__name__`、
  `__module__`、方法表、descriptor 都应进入这条 typed dict 路径，而不是
  放在 VM、builtin helper 或 C++ side table 中。
- `TypeSlots` 是低层对象协议调度表，不是 Python 可见 namespace。像
  `list.append` 这样的内建方法必须作为 descriptor object 放进
  `list_type->dict`，实例访问时由 descriptor 协议生成 bound callable；
  不允许为每个 Python 方法新增临时 slot。
- `TypeFlags` 记录类型布局能力。紧凑内建实例默认不带 arbitrary instance
  dict；heap type 默认带 `HasInstanceDict`，普通用户对象属性写入才会懒建
  `Object::instance_dict`。模块对象和类型对象有自己的 namespace 路径，不依赖
  普通实例字典。
- Builtin method binding follows CPython's split: class access such as
  `list.append` returns a `method_descriptor`; instance access such as
  `[].append` returns a `builtin_function_or_method`-typed callable carrying
  both descriptor and receiver, and it must traverse both references for GC.
- Get/set descriptors represent typed runtime attributes such as `__class__`.
  They are ordinary descriptor objects in type dictionaries, with readable and
  settable behavior expressed through `descr_get`/`descr_set`; special
  attributes must not be implemented as VM or opcode shortcuts.
- `__dict__` visibility is also descriptor-driven. Type objects expose their
  runtime dictionaries through `mappingproxy`, not raw mutable dict objects;
  heap instances expose their own mutable instance dict; compact builtins
  without `HasInstanceDict` report no `__dict__`.
- Attribute lookup 的入口是统一 `object_get_attr` API。当前最小顺序是：
  module dict -> data descriptor -> instance dict -> non-data descriptor/type
  attribute -> metatype MRO dict。后续 `__getattr__`、slot wrappers 和
  mappingproxy 必须扩展这条 API，而不是在 opcode 或 builtin 中另开查找路径。
- Type-object lookup follows the same descriptor model with the metatype data
  descriptor priority CPython requires: metatype data descriptor -> class MRO
  attribute -> metatype non-data attribute. This is what makes
  `list.__class__` resolve to `type` while `list.append` still comes from the
  `list` type dictionary.
- Descriptor access is also centralized through `object_get_attr`: values found
  in type/class dictionaries with a `descr_get` slot are invoked with
  `(descriptor, instance, owner)`. Module namespace entries are direct values;
  module type descriptors still flow through type lookup.
- Object instance dictionaries are optional and lazily allocated through
  `object_set_attr`; they are traversed by the base object traversal path so
  future GC does not need per-type special cases for normal instance state.
- Slot 扩展必须按协议族推进，不能每个 opcode 单独加临时入口。例如数值
  协议应一次性规划 normal/reflected/in-place，比较协议应返回 Python bool 或
  NotImplemented 语义。
- `TypeSlots` must be initialized with designated fields in factories and
  tests. Positional aggregate initialization is forbidden because new slots can
  otherwise silently shift behavior or encourage placeholder `nullptr` clutter.
- 模块、函数、code、frame、异常等运行时对象都应是普通 typed objects，
  让脚本层反射、调试、traceback 和扩展 API 共享同一对象模型。
