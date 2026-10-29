# 架构设计

## 目录结构

尽量对齐 CPython，但允许 C++ 风格实现：

```text
Include/        public and internal headers
Objects/        core object implementations: object, type, int, str, dict, module, descriptors
Python/         compiler, bytecode, VM, runtime state, import core, GC core
Parser/         lexer, parser, AST builder
Modules/        CPython-aligned builtin/native module substrate: sys, posix, _io, gc, ...
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
   模块是 typed module object。Public stdlib 模块默认来自 `Lib/*.py`；
   native 模块只允许作为 CPython 已定义的 builtin/substrate 模块或不可避免的
   bootstrap seed 存在，例如 `sys` 提供运行时状态、`posix` 提供宿主平台调用、
   `_io` 提供 IO substrate。Import system 逐步对齐 importlib，但核心
   bootstrap 需要最小、可测试。

10. C/C++ extension layer
   先定义稳定最小 API：object handles、type creation、module definition、argument parsing、exception setting、reference/root guard。C++ binding API 构建在 C API 之上。

## 源码边界

- `Objects/` 只保存 Python object model 的核心布局、协议和基础内建对象。
  不把 import machinery、stdlib module registry、POSIX/IO host hooks 塞进
  `Objects/`。如果一个文件主要在回答“如何加载模块、如何执行代码、如何访问宿主
  OS”，它不属于 `Objects/`。
- `Python/` 保存解释器内核：runtime bootstrap/accessors/factories/rooting、
  VM、frame/thread state、compiler/symbol table、import core、GC core 和
  builtins bootstrap。这里可以依赖内部 C++ 实现，但仍要保持模块边界清晰。
- `Modules/` 保存 CPython 已定义的 native/builtin substrate。`sys`、`gc`、
  `posix`、`_io`、`_collections` 等应按模块拆分，而不是聚合成一个
  `runtime_stdlib_modules.cpp`。public policy 仍优先在 `Lib/`。
- 当前规模下，每个 native module 优先使用 `Modules/<module>.cpp` 这种直接
  文件名，例如 `Modules/_io.cpp` 同时保存 `_io` 初始化和薄的
  `TextIOWrapper` substrate。只有当模块内部已经自然分裂出多个独立子组件时，
  才建立子目录。
- 新增 native module 前必须先核对 CPython 的模块名和层次。没有 CPython
  对应物时，默认选择 `Lib/` 脚本实现，而不是发明新的 C++ hook。
- C/C++ extension ABI 不能暴露内部模板 RAII 类型、`Rooted<T>`、STL 容器布局
  或 C++ class layout。公开边界使用窄的函数表、不透明句柄和显式错误返回；
  RAII 可以作为 core 内部实现细节存在。

## 自举原则

- C++ bootstrap 只负责建立不可避免的种子对象：`object`、`type`、核心 slots、基础异常、最小 VM 和 import 前置环境。
- 一旦 VM、code object、module object 和 import core 可用，后续 builtins、标准库子集、测试脚本和开发工具应尽量通过运行时自身加载和执行。
- 手写 bytecode tests 是早期锁定 VM 语义的工具，不是长期执行入口。长期入口必须走 parser/compiler/code object/module 执行链。
- 每个新的运行时能力都要回答：它是 bootstrap seed，还是应该由 Python 层自举出来。如果是 seed，必须保持小而稳定。
- Public 标准库 API 不应直接落在 C++ 注册表中。C++ 可以暴露 `_io`
  这类 CPython 已有的私有、窄口、真实能力 hook；也可以暴露 `sys`、`gc`、
  `posix` 这类 CPython 定义为 builtin 的底层模块。策略、兼容别名、
  参数默认值、helper class 和组合逻辑应写在 `Lib/` 中。新增底层模块名
  必须先核对 CPython，不能随意发明私有前缀。

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
`traverse`、`binary_op`、`reflected_binary_op`、`inplace_binary_op`、`unary_op`、`call`、
`length`、`getitem`、`setitem`、`contains`、`descr_get`、`descr_set`、`new_object`、
`init`。后续扩展顺序：

- Numeric: C++ 层使用 `BinaryOp` 统一表示 add/sub/mul/div/mod/pow，并通过
  `object_binary_op` 调度。VM 层应使用携带 `BinaryOp` operand 的通用
  `BINARY_OP` 形态，parser/compiler 层按优先级把 `+`、`-`、`*` 等语法降级
  到同一协议。一元运算通过 `UnaryOp`、`unary_op` 和 `UNARY_OP` 调度。
  `NotImplemented`、subclass priority、reflected/in-place fallback 已接入
  `object_binary_op`/`object_inplace_binary_op`，不能散落在各个内置类型中。
- Rich compare: `object_rich_compare` 支持 `NotImplemented`、reverse/subclass
  dispatch 和 equality fallback；`==`、`!=`、`<`、`<=`、`>`、`>=` 语法降级到
  `COMPARE_OP`。比较链由 compiler lowering 成显式栈操作和短路跳转，保持
  CPython 语义：中间 operand 只求值一次，失败后不继续求后续 operand。
- Sequence/mapping: length, getitem, setitem, contains, iter, iternext。
  `object_iter`/`object_next` are the only runtime entrypoints for consuming
  iterable objects. `list(iterable)`, future `for` lowering, call `*args`
  expansion, and generator protocol work must use these APIs rather than
  recognizing concrete container layouts.
- Attribute/descriptor: getattro/setattro, descr_get/descr_set。
- Call/construction: `type` 的 call slot 统一调度目标 type 的 `new_object` 和
  `init` slots。Python 类、heap type 和内建类型构造都必须接这条路径；
  `class` statement、builtin constructors 和 C/C++ extension type creation 不应
  各自实现对象分配。
- Buffer/extension hooks: buffer protocol 和 C/C++ extension 需要的稳定边界。

## VM、栈和脚本层开放性路线

- `CodeObject` 是脚本执行的稳定载体，必须逐步补齐 CPython 类似的元数据：
  `name`、`filename`、line table、constants、names、varnames、freevars、
  cellvars、flags。任何 CLI、traceback、function object、debug API 都不应
  绕过 `CodeObject` 私下保存这些执行信息。
- 当前 `CodeObject` line table 使用与 instruction stream 等长的
  per-instruction line vector。Compiler 必须通过统一 `emit()` 写入指令和行号；
  `CodeObject` 构造时验证二者长度一致。缺失 line table 仅允许在手写 bytecode
  测试或 bootstrap seed 中出现，parser/compiler 生成的脚本 code 必须携带
  source line。
- `Frame` 是运行时可观察对象，不是临时 C++ stack helper。Frame 必须明确
  持有 code、locals、globals、builtins、value stack、block stack、last
  instruction/line。当前 VM 可以保留宿主执行器，但脚本层观察执行状态时
  应通过 frame/code/module 对象，而不是暴露 `VirtualMachine` 内部结构。
- Python exception object may temporarily carry the first failing frame's
  filename/code-name/line as a traceback seed. This is only the minimum CLI
  debugging surface; full traceback objects must later be modeled as ordinary
  runtime objects linked from exceptions, with frame/code traversal and
  script-visible attributes.
- Value stack 只保存 `Object*`，所有跨 GC/分配边界的活跃对象必须能从
  active frames traversal 访问到。后续引入 GC 时，active frame stack、
  block stack、exception state 都必须成为根。
- C++ 临时对象必须使用 `Rooted<T>` 跨越可能分配或触发 GC 的代码段。Runtime
  roots 是 counted entries，允许同一对象被嵌套 guard 多次保护；guard 析构
  只移除自己对应的一层根。不要依赖手写成对 `add_root/remove_root` 作为长期
  模型。
- VM opcode handlers are responsible for rooting values after they are popped
  from the frame value stack and before they are passed to object protocols or
  allocation-heavy constructors. This includes aggregate literal elements,
  call operands, class creation temporaries, attribute/subscript operands, and
  truth/comparison operands. Values should remain rooted until the result is
  pushed back onto the active frame or moved into a traversable runtime object.
- Name resolution 必须区分编译期 fast locals 和动态 namespace lookup。函数
  scope 中编译期判定为 local 的名字使用 `LOAD_FAST`/`STORE_FAST`，只访问
  frame locals，缺失时抛 `UnboundLocalError`；模块和 class body 的动态名字
  使用 `LOAD_NAME`/`STORE_NAME`，按 locals -> globals -> builtins 查找。opcode、
  builtin helper、module execution 不能各自实现一套查找。
- VM opcode 只负责调度和栈效果，类型语义由 object/type slots 或统一 runtime
  API 完成。新增 opcode 必须同时更新 stack effect、disassembly/metadata、
  compiler emission、VM execution 和测试。
- Arithmetic opcode dispatch goes through `BINARY_OP(BinaryOp)` and then
  `object_binary_op`; unary opcode dispatch goes through `UNARY_OP(UnaryOp)`
  and then `object_unary_op`. The old add-only opcode/API/AST path has been
  removed from active code and must not be reintroduced.
- `COMPARE_OP` 的职责是弹出左右操作数，解码比较 operator，调用
  `object_rich_compare`，再把 Python bool/object 结果压栈。比较语义不能在
  VM 中直接调用 `object_equal` 或识别 `int`/`str` 等具体类型。
- Jump opcodes are structural VM control flow. `JUMP_ABSOLUTE` rewrites the
  frame instruction pointer after validating the target; conditional jumps pop
  one Python object and dispatch truth through `is_true`/type truth slots.
  Parser/compiler control-flow syntax must lower to these opcodes instead of
  embedding truth or branch behavior outside the VM.
- `CALL`/`CALL_KW` 的职责是栈调度：弹出 positional arguments、可选 keyword
  dict 和 callable，组装 `CallArguments`，调用统一 `object_call`/type call
  slot，再把返回值压栈。函数对象、native function、type construction、
  bound method 都必须接入这条协议，而不是让 opcode 识别具体对象类型。
- Exception control flow is VM block-stack behavior. `SETUP_EXCEPT` records a
  handler target on the frame, `RAISE_VALUE` normalizes a `BaseException`
  instance or exception type into an `ExceptionObject`, and the VM catch path
  transfers control to the nearest handler with the exception object on the
  value stack. Handler matching uses `CHECK_EXCEPTION_MATCH` against real
  exception types. Future `finally`, bare re-raise, exception groups,
  traceback objects, and exception state must extend this block/exception
  model rather than adding parser, builtin, or CLI special cases.
- Function signature metadata belongs in `CodeObject`, not in ad hoc function
  side tables. `arg_count` records positional parameters; flags record
  `*args` and `**kwargs`; `varnames` layout is positional parameters, varargs
  name, varkeywords name, then ordinary locals. Future defaults,
  keyword-only parameters, positional-only markers, annotations, freevars, and
  cellvars must extend this metadata coherently.
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
- Tuple and dict literals are normal expression AST nodes, not prebuilt
  constants. The compiler lowers them to `BUILD_TUPLE` and `BUILD_MAP` after
  emitting element/key/value expressions in source order, preserving dynamic
  evaluation semantics and dict insertion order.
- `True`、`False` 和 `None` 是 lexer/parser 层的 singleton literals，而不是
  ordinary names。它们应编译为直接加载 runtime singleton，赋值目标转换必须
  拒绝这些表达式；不要通过 builtins name lookup 模拟这些常量。

## 模块系统路线

- 模块是 typed runtime object，模块 dict 是脚本层可见 namespace。CLI、
  import、`sys.modules`、package execution 都应围绕 module object 工作，
  不再退回裸 dict 模块模型。
- Public stdlib-first rule: `sys`、`os`、`inspect`、`gc`、`collections` 等
  对外模块应优先是 `Lib/*.py`。C++ 层只提供私有 hook 模块和不可替代的
  runtime/kernel 对象；不能把 public API 面逐项塞进
  `Runtime::import_stdlib_module`。
- Native hook modules must expose real substrate only. A hook such as
  `posix.listdir` may call the host filesystem and raise Python exceptions; it
  must not return placeholder values to make an API appear supported.
- `sys.builtin_module_names` lists native/builtin module names, not public
  script wrappers. Public wrappers become visible through normal import and
  `sys.modules`.
- 模块初始化最小字段包括 `__name__`、`__file__`、`__builtins__`。后续导入
  系统加入 `__spec__`、`__package__`、`__loader__`、`__path__`。
- Builtins 应首先作为模块对象存在，再由 frame name resolution 的 builtins
  dict 暴露给脚本。不要在 VM 或 CLI 特判 `print`、`len` 等名字。
- 最小 native builtins 可以作为 bootstrap seed 存在，但必须是普通 typed
  objects，放入 `builtins` module dict，并通过 `LOAD_NAME`/`CALL` 访问。标准
  行为扩展时优先补通用协议，例如 `str()`、keyword arguments、stdout/file
  对象，而不是继续扩展 `print` 的内部特例。
- Native builtin 和 constructor 的参数校验必须走共享 helper/API，而不是在
  每个 builtin 中手写字符串拼接。当前 helper 覆盖 keyword rejection 以及
  no/exact/at-most positional checks；后续 C/C++ extension argument parsing
  应在这一层扩展 optional、keyword-only、default 和 conversion support。
- Iterable-consuming builtins must use the iterator protocol. `list(iterable)`
  consumes `object_iter`/`object_next` until `StopIteration`; future builtins
  such as `tuple`, `dict`, `sum`, `any`, `all`, and call expansion must follow
  the same path rather than branching on list/tuple/dict concrete layouts.
- `repr()` and `hash()` are ordinary native builtins that dispatch to object
  protocol helpers. They are not CLI formatting shortcuts. `None`、`True`、
  `False` and core type objects live in the builtins module dict so script
  execution and host tests observe the same namespace shape.
- Core builtin constructors must be type calls wherever possible. `int()`,
  `bool()`, `str()`, `list()`, `tuple()`, and `dict()` use type
  `new_object`/`init` slots, not native function stand-ins. Iterable
  constructors such as `list(iterable)` and `tuple(iterable)` consume
  `object_iter`/`object_next`; they must not branch on concrete container
  layouts.
- Builtin exception types are normal type objects in the builtins module. New
  semantic error categories such as `ValueError` should be added to the
  exception hierarchy before APIs start faking them with unrelated exception
  classes.
- REPL execution uses the same compiler and module execution path as scripts.
  Single-line expressions may be compiled with `compile_eval_input` for
  display, but statements and compound input must run through
  `compile_exec_input` in a persistent `__main__` module. The CLI must not
  implement a separate evaluator.
- Introspection builtins such as `globals()` and `locals()` read the active
  frame rather than CLI globals. `isinstance` and `issubclass` dispatch through
  the type/MRO helpers, including tuple classinfo recursion, so future heap
  types and extension types share one semantic path.
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
- `TypeObject::solid_base` 记录实例内存布局根。普通 heap/object-layout 类的
  solid base 是 `object`；`list`、`dict`、`tuple`、`str` 等紧凑内建类型的
  solid base 是对应内建类型；共享同一 C++ layout 的异常层级共享
  `BaseException` solid base。多继承创建类型前必须验证所有非 `object`
  solid base 相同，否则抛 `TypeError`，不能先创建一个 slots/布局不一致的
  半合法类型。
- `TypeObject::base` 是 layout/slot inheritance 的 best base cache，不等于
  源码 `__bases__` 的完整语义；`bases` 和 `mro` 仍是继承和属性查找的语义
  来源。选择 best base 时优先选有非 `object` solid base 的基类，保证
  `object` 与紧凑内建混排时不会继承错误 slots。
- Builtin method binding follows CPython's split: class access such as
  `list.append` returns a `method_descriptor`; instance access such as
  `[].append` returns a `builtin_function_or_method`-typed callable carrying
  both descriptor and receiver, and it must traverse both references for GC.
- Slot wrappers follow the same descriptor rule. Low-level `TypeSlots` remain
  the C++ protocol table; script-visible special methods such as `__len__`,
  `__getitem__`, `__setitem__`, `__delitem__`, `__contains__`, `__iter__`,
  `__next__`, numeric dunders, and comparison dunders are represented by
  `wrapper_descriptor` objects in type dictionaries. Instance access returns a
  `method-wrapper` object carrying the wrapper and receiver; direct class calls
  validate the receiver against the wrapper owner before invoking the owner's
  raw slot. This keeps opcode dispatch, builtins, and script-visible dunders on
  one protocol surface without teaching the VM about concrete layouts.
- Get/set descriptors represent typed runtime attributes such as `__class__`.
  They are ordinary descriptor objects in type dictionaries, with readable and
  settable behavior expressed through `descr_get`/`descr_set`; special
  attributes must not be implemented as VM or opcode shortcuts.
- `__dict__` visibility is also descriptor-driven. Type objects expose their
  runtime dictionaries through `mappingproxy`, not raw mutable dict objects;
  heap instances expose their own mutable instance dict; compact builtins
  without `HasInstanceDict` report no `__dict__`.
- Attribute lookup 的入口是统一 `object_get_attr` API。公共入口先检查用户
  覆盖的 `__getattribute__`；默认路径是 `object_get_attr_default`，顺序是：
  module dict -> data descriptor -> instance dict -> non-data descriptor/type
  attribute -> metatype MRO dict。若公共入口捕获到 `AttributeError`，再运行
  `__getattr__` fallback。后续 slot wrappers 和 mappingproxy 必须扩展这条 API，
  而不是在 opcode 或 builtin 中另开查找路径。
- Type-object lookup follows the same descriptor model with the metatype data
  descriptor priority CPython requires: metatype data descriptor -> class MRO
  attribute -> metatype non-data attribute. This is what makes
  `list.__class__` resolve to `type` while `list.append` still comes from the
  `list` type dictionary.
- Descriptor access is also centralized through `object_get_attr`: values found
  in type/class dictionaries with a `descr_get` slot are invoked with
  `(descriptor, instance, owner)`. Module namespace entries are direct values;
  module type descriptors still flow through type lookup.
- `object.__getattribute__` is a normal method descriptor on `object` and calls
  `object_get_attr_default`. This deliberately bypasses `__getattr__`, while
  public `object_get_attr` owns the `AttributeError -> __getattr__` behavior.
- `__getattr__` is a final fallback in `object_get_attr`, not an opcode
  behavior. Instance fallback uses direct special lookup on the owner type and
  then calls the bound result with the missing attribute name. Module fallback
  calls a module dictionary `__getattr__` directly, without descriptor binding.
- Descriptor assignment and deletion are centralized through `object_set_attr`
  and `object_delete_attr`. Data descriptors receive normal assignment with a
  non-null value and deletion as `descr_set(..., value=nullptr)`. Public
  `object_set_attr` and `object_delete_attr` first honor user-defined
  `__setattr__` / `__delattr__`; `object.__setattr__` and `object.__delattr__`
  are normal method descriptors that call the default APIs. `STORE_ATTR` and
  `DELETE_ATTR` bytecodes call the public APIs rather than deleting from
  dictionaries directly.
- Item access follows the same protocol family: `object_get_item`,
  `object_set_item`, and `object_delete_item` dispatch through `TypeSlots`
  (`getitem`/`setitem`/`delitem`). `BINARY_SUBSCR`, `STORE_SUBSCR`, and
  `DELETE_SUBSCR` must remain thin VM dispatches and never special-case list,
  dict, tuple, or extension layouts.
- Parser and compiler delete handling must use the assignment-target family.
  `del name`, `del obj.attr`, and `del obj[key]` lower to dedicated bytecodes;
  future multi-target/destructuring deletion should extend that target visitor
  instead of injecting synthetic `__delattr__`/`__delitem__` calls.
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

## Python 类系统实现路线

- 第一阶段先完善“当前类的方法”基础设施：函数对象、bound method、builtin
  method descriptor、getset/member descriptor、slot wrapper 都必须是普通
  descriptor object，并统一接入 `object_get_attr`、`object_set_attr` 和
  `object_call`。不要在 VM 或 parser 里为 `list.append`、`__class__`、
  `__dict__`、`__new__`、`__init__` 加私有捷径。
- Python 函数对象应持有 `CodeObject`、globals、defaults、kwdefaults、
  annotations、closure 和 `__qualname__`。函数作为类属性读取时通过 descriptor
  返回 bound method；作为模块变量或普通值调用时直接走 call slot。
- Python function call must reuse the currently active VM when called from
  bytecode, so nested frames remain in one `ThreadState` for GC roots,
  traceback, tracing, and future generators. Host-side calls may create a VM
  entrypoint, but opcode calls must not silently fork execution state.
- Function definitions lower to a code object constant plus `MAKE_FUNCTION`,
  then ordinary `STORE_NAME`. This keeps function creation in the VM/runtime
  path where globals and active frame context are available; the compiler must
  not pre-build function objects with placeholder globals.
- Function code objects store parameters first in `varnames`, followed by
  compiler-discovered local assignment names. The current scope analysis is a
  strict first slice: parameters, assignment targets, nested `def`, and nested
  `class` bind fast locals in function scope; module/class scopes remain
  dynamic namespaces. `global`、`nonlocal`、freevars/cellvars 和 closure objects
  must extend this analysis instead of reintroducing dynamic function locals.
- `type.__call__` 是对象构造唯一入口：选择 `__new__`，再在返回对象是目标
  类型实例时调用 `__init__`。内建类型构造和 Python 类构造都必须复用这条
  call/new/init 协议。
- `type(obj)` 是 `type` call path 的 introspection 形式，直接返回对象的
  runtime type。三参数 `type(name, bases, namespace)` 必须与 class statement
  共用 metaclass selection、C3 MRO 和 namespace validation，不允许单独做一条
  简化创建路径。
- `class` statement 编译为“执行 class body code object -> 收集 namespace ->
  调用 metaclass”的路径。class body locals 是类 namespace，不是模块 globals；
  `__module__`、`__qualname__`、方法函数对象和 descriptors 都写入该 namespace。
- 当前实现通过 `ClassConstructionRequest` 统一 class statement、
  `type(name, bases, namespace)` 和 `Meta(name, bases, namespace)`。`MAKE_CLASS`
  只负责求值 bases、执行 class body，并把 request 交给 runtime；三参数
  type/metaclass call 也构造同一 request。后续 `metaclass=`、class keywords
  和 `__prepare__` 必须扩展 request/prepare path，而不是在 VM、parser 或
  factory 中新增并行创建逻辑。
- Class statement lowering evaluates base expressions first, builds a bases
  tuple, then executes the class body with an isolated namespace. `MAKE_CLASS`
  consumes that bases tuple and the class body `CodeObject`, so no-base and
  with-base class statements share the same VM contract.
- 继承采用 tuple `__bases__` 和 C3 linearization。单继承路径不能固化成长期
  设计；`TypeObject::base` 只能作为快速主基类缓存，真实语义以 `bases` 和
  `mro` 为准。
- C3 MRO 已作为 heap type 初始化的一部分存在。后续要补的是
  metaclass-derived MRO override、class keyword arguments 和 full
  `__prepare__` invocation。
- 元类选择遵循 CPython 的核心 winner 规则：显式 metaclass seed 优先，否则取
  所有 base 的 metatype 的最派生公共子类型；存在冲突时抛 `TypeError`。当前
  `Meta = type('Meta', (type,), {})`、`Meta('Base', (), {})` 和继承 Base 的
  class statement 已走同一 selection path。语法层 `metaclass=` 和
  `__prepare__` lookup/call 是下一步。
- heap type 与内建静态类型共享同一 attribute engine。差别只在 layout flags、
  slot table 和 allocation policy；Python 层可见 attrs 一律来自 type dict、
  instance dict、descriptors 和 MRO。
- `__slots__` 后置到 heap type layout 稳定后实现，但现在的 instance dict 设计
  必须允许以后区分 dict-backed instance、slot-only instance 和二者并存的实例。
- `super`、`classmethod`、`staticmethod`、`property` 都应作为 descriptor
  协议的自然结果实现。`property`、`staticmethod` 和 `classmethod` 当前已经是
  typed descriptor objects；decorator syntax 编译为普通 callable application
  和 class-body rebinding，不绕过 descriptor/type attribute engine。
- `property` is a typed data descriptor stored in a class dictionary. Class
  access returns the descriptor; instance access calls the getter with the
  instance; assignment calls the setter or raises `AttributeError` for a
  read-only property. `property.getter`、`property.setter` 和
  `property.deleter` 是 `property` 类型字典中的普通 method descriptors；它们
  返回新的 property descriptor 并保留未替换的 accessor/doc slot。Future
  descriptor-family builtins must follow this object/type-slot path rather
  than adding VM or compiler cases.
- `staticmethod` is a non-data descriptor that returns its wrapped callable for
  both class and instance access. `classmethod` binds the owner type as the
  method self for class and instance access. Runtime method objects therefore
  bind generic callable/self pairs instead of assuming every bound callable is
  a Python function.
- Function and class decorators are not a descriptor special case. Parser
  records decorator expressions, the compiler emits them in source order, then
  applies them through repeated one-argument calls after `MAKE_FUNCTION` or
  `MAKE_CLASS`; the resulting object is stored through the same binding path as
  an undecorated definition.
