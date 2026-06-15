# cxxpython 优化计划

> 状态：执行中 | 更新：2026-06-15 | 分支：fix-thread-improve

## 概述

在完成强所有权迁移、GC clear phase、TypeObject 迁移后，剩余优化集中在：
1. VM 调度器改为 CPython 式宏展开 + 全 opcode computed goto
2. GC 标记数据结构从 unordered_set 改为 per-object mark bit
3. 收敛锁语义（去 recursive_mutex、ThreadState 无锁化）
4. decref 从 CAS loop 改为 fetch_sub
5. 分配池 / free list
6. 代际 GC + remembered set

---

## Phase 1: VM 宏展开 dispatch + 全 computed goto [执行中]

**当前状态**：
- `Python/vm.cpp:1128-1403` computed goto 覆盖 10/59 opcode，其余回落 switch
- dispatch table 手动维护，代码在 computed goto 标签和 switch case 间重复
- `CXXPYTHON_HAS_COMPUTED_GOTO` 在 `CMakeLists.txt:82` 对 GCC/Clang 设为 1

**目标设计**：
- 每个 opcode 用 `CXXPYTHON_TARGET(OpName)` 宏包裹，`CXXPYTHON_DISPATCH()` 统一跳转
- 两种模式（switch/computed goto）从同一份 opcode 代码体展开，零重复
- 59/59 opcode 全覆盖 computed goto
- eval breaker 内联到 `CXXPYTHON_DISPATCH()` 中

**文件变更**：
- 新建 `Python/vm_dispatch.hpp`：宏定义
- 重写 `Python/vm.cpp` 主循环：删除现有 `#ifdef` 分支 + `dispatch_table[]` + 重复代码
- 每个 opcode 的 handler 在单一 code body 内，由宏展开为 switch case 或 computed goto label

**验证**：
- `cmake --build build -j2` 编译通过（GCC/Clang computed goto，MSVC switch）
- `ctest --test-dir build --output-on-failure` 全部通过
- `./build/cxxpython build/test2.py multi 1000000` 性能基准 < 当前

---

## Phase 2: GC mark-bit + 去 recursive_mutex + ThreadState 无锁化 [等待]

### 2a: GC mark-bit

**当前**：`std::unordered_set<Object *> marked` (`Python/gc.cpp:20`)
- 每 GC 分配哈希表节点，O(N) hash + probe
- `marked.find()` 每次 hash 计算

**设计**：
- `Object::gc_state_` (1 byte)：bit0=marked, bit1=generation(Old), bit7=finalizing
- 替代 `GCGeneration gc_generation_` 和新增 mark bit
- `enqueue` 直接 `object->gc_mark()`（位操作，无哈希）
- sweep 阶段 `!object->gc_marked()`（单次位测试）

**文件**：`Include/cxxpython/object.hpp`, `Python/gc.cpp`

### 2b: 去 recursive_mutex

| 锁 | 当前 | 改为 |
|----|------|------|
| `heap_mutex_` | `std::recursive_mutex` | `std::mutex` |
| `roots_mutex_` | `std::recursive_mutex` | `std::mutex` |
| `gc_mutex_` | `std::recursive_mutex` | `std::mutex` |
| `gc_coordination_mutex_` | `std::recursive_mutex` | `std::mutex` |
| `thread_states_mutex_` | `std::recursive_mutex` | `std::mutex` |

**文件**：`Include/cxxpython/runtime.hpp`, `Python/gc.cpp`

### 2c: ThreadState 无锁化

- `ThreadState::gc_state_` → `std::atomic<ThreadGCState>`（替换 `mutex_` 保护）
- `push_frame/pop_frame` 去 `mutex_` lock（帧栈仅拥有线程或 parked 时的 GC 线程访问）
- `set_trace_function/add_temporary_root/remove_temporary_root` 去锁
- `handled_exceptions_` push/pop 去锁（仅拥有线程访问）
- `ThreadState::mutex_` 完全删除

**文件**：`Include/cxxpython/vm.hpp`, `Python/vm.cpp`

**验证**：
- 全量 ctest 通过
- 并发 GC 测试 `gc_thread_wait.py` 通过
- benchmark 无退化

---

## Phase 3: decref fetch_sub + eval breaker 合并 [等待]

### 3a: decref 精简

**当前**：`decref()` CAS loop（`Objects/object_core.cpp:1033-1043`）

**改为**：单条 `fetch_sub(1, relaxed)`，无 while 循环

```cpp
void Object::decref() noexcept {
    ref_count_.fetch_sub(1, std::memory_order_relaxed);
}
```

**文件**：`Objects/object_core.cpp`

### 3b: eval breaker 合并

**当前**：`eval_breaker_requested()` 三个独立条件检查（`Python/vm.cpp:241`）

**改为**：单个 `atomic<bool>` 的 `load(relaxed)`，设置时 OR 三个条件

**文件**：`Python/vm.cpp`

**验证**：
- ctest 全部通过
- benchmark 预期 2-5% 提升

---

## Phase 4: 分配池 + VM 微优化 [等待]

### 4a: Per-type Thread-local Free List

```cpp
template <class T>
struct ObjectFreeList {
    static constexpr std::size_t kMaxFree = 80;
    inline static thread_local std::vector<std::unique_ptr<T>> free_list_;
    // allocate / deallocate
};
```
覆盖类型：`IntObject`, `FloatObject`, `StrObject`, `ListObject`, `DictObject`, `TupleObject`

**文件**：`Objects/object_utils.hpp`, `Python/runtime.hpp` (`Runtime::allocate<T>` 使用 free list)

### 4b: VM 微优化

| 编号 | 优化 | 预期增益 |
|------|------|----------|
| VM1 | `CALL` native function 快速路径 | ~5% |
| VM2 | `LOAD_CONST` 内联 | ~1% |
| VM3 | `COMPARE_OP` 整数快路径移到 computed goto label | ~2% |
| VM4 | `LOAD_FAST` nullptr 检查提前 | 已有 |
| VM5 | `FOR_ITER` range 快路径扩展到 list/tuple | ~3% |
| VM6 | eval breaker 单 atomic load | ~0.5% |

**文件**：`Python/vm.cpp`, `Python/vm_fastpaths.cpp`

**验证**：
- ctest 全部通过
- benchmark 预期 5-10% 提升

---

## Phase 5: 代际 GC + Remembered Set [等待]

### 5a: Write Barrier

当 Old 代对象写入 Young 代目标时，将 Old 代对象加入 `remembered_set_`：

- `dict->set`、`list->set`、`list->append`、`Frame::set_fast_local`、`Cell::set_value`
- 通过 `write_barrier(Object *src, Object *dst)` 内联函数

### 5b: Young GC

- young GC 只 trace：
  1. 全局 roots
  2. `remembered_set_` 中的 Old 代对象
  3. 线程帧栈 + 临时根
- 不扫描全堆（消除 `Python/gc.cpp:32-38` 旧代全扫描）
- 不 mark Old 代对象（跳过不可达 Old 对象）

**文件**：`Python/gc.cpp`, `Objects/object_mapping.cpp`, `Objects/object_sequences.cpp`, `Python/vm.cpp`

**验证**：
- 代际正确性测试（old-to-young ref + young-only GC）
- benchmark 预期 GC 停顿 -80%

---

## 文档更新

Phase 全部完成后更新 `doc/` 下文档：

- `doc/development_status.md`：更新性能基准、已完成特性
- `doc/development_plan.md`：将本计划中已完成项标记、调整剩余计划
- `doc/architecture_design.md`：补充 VM 宏展开设计、GC mark-bit 设计

---

## 进度追踪

| Phase | 状态 | ctest |
|-------|------|-------|
| Phase 1 VM 宏展开 | ✅ 完成 | 244/244 |
| Phase 2 GC mark-bit + 锁收敛 + ThreadState 无锁 | ✅ 完成 | 244/244 |
| Phase 3 decref fetch_sub | ✅ 完成 | 244/244 |
| Phase 4 分配池 + VM 微优化 | ⏳ 等待 | - |
| Phase 5 代际 GC + remembered set | ⏳ 等待 | - |

### Phase 1 完成细节
- 新增 `CXXPYTHON_OP_LABEL(op)` / `CXXPYTHON_OP_ENTRY(op)` 宏，59/59 opcode 全覆盖 computed goto
- 旧 `#ifdef` 块（1127-1412行）的重复 handler 代码与 `dispatch_table[]` 被统一 dispatch 替代
- computed goto label 直接落在 switch `case` 标签前，消除代码重复

### Phase 2 完成细节
- `Object::gc_state_` (1 byte) 替换 `GCGeneration gc_generation_`：bit0=marked, bit1=old
- GC 标记从 `std::unordered_set<Object*>` 改为 per-object mark bit + `std::vector<Object*>` marked list
- 移除 `#include <unordered_set>`，sweep 中用 `!object->gc_marked()` 替代 `marked.find()`
- 5 个 `std::recursive_mutex` → `std::mutex`（heap/roots/thread_states/gc/gc_coordination）
- `std::condition_variable_any` → `std::condition_variable`
- `ThreadState::mutex_` 完全删除；`gc_state_` 改为 `std::atomic`（acquire/release ordering）
- 所有 ThreadState 方法去锁化（仅拥有线程或 parked GC 线程访问）

### Phase 3 完成细节
- `decref()`: CAS loop → `fetch_sub(1, relaxed)`（单条原子指令）
