# No-GIL GC and Threading Design

> Legacy RC/cycle-GC proposal for the old runtime. It does not govern the
> cxxpython::core foundation. The authoritative
> `cxxpython_foundation_redesign_plan_2.md` retains non-moving precise tracing
> without Python object RC and requires multiple mutators in one shared heap.

This document defines the target ownership, garbage collection, and threading
model for cxxpython without a global interpreter lock. It is a design contract
for implementation work, not a benchmark workaround. The goal is to make object
lifetime deterministic, make cycle collection bounded and explicit, and keep
multi-threaded execution correct without serializing all Python code behind one
runtime-wide lock.

## Core Position

cxxpython should use a CPython-like ownership model:

- Reference counting owns ordinary object lifetime.
- Cycle GC collects unreachable reference cycles.
- Stop-the-world coordination is used only for cycle GC and rare global
  transitions.
- Containers protect their own invariants with object-level synchronization.
- Leaf temporaries such as most integers and floats must not accumulate until a
  full GC sweep.

The current runtime treats reference counts mostly as metadata and relies on GC
sweep to delete zero-reference objects. That is not a no-GIL design. It creates
large temporary heaps, makes explicit `gc.collect()` scan unrelated leaf
objects, and moves ordinary numeric cleanup onto the slowest possible path.

## Goals

- Run Python bytecode on multiple OS threads without a global interpreter lock.
- Make `incref()` and `decref()` real ownership operations.
- Avoid recursive destruction in arbitrary caller contexts.
- Keep all GC-visible roots explicit and testable.
- Make cycle GC trace only objects that can participate in cycles.
- Make thread stop coordination bounded: every running Python thread must reach
  a safepoint or publish a stable native-wait state.
- Preserve Python semantics before adding optimistic fast paths.
- Keep performance gates close to correctness gates.

## Non-Goals

- A concurrent moving/tracing collector is not the first target.
- Lock-free `dict` and `list` mutation is not required.
- Benchmark-specific type or script special-cases are forbidden.
- A global coarse runtime lock that effectively recreates the GIL is not an
  acceptable no-GIL solution.

## Object Header

Every heap object has:

- `Runtime *runtime`
- `TypeObject *type`
- atomic reference count
- GC/lifetime state bits

Required state bits:

- `Tracked`: object may own references that can form cycles and is visible to
  cycle GC.
- `Marked`: temporary mark bit used by stop-the-world cycle GC.
- `Old`: generational state for tracked objects.
- `Finalizing`: object is currently clearing owned references.
- `QueuedForDealloc`: object has already been scheduled for deferred
  deallocation.
- `Immortal`: object has process/runtime lifetime and is never deallocated by
  normal refcount or GC paths.

These bits are independent. In particular, `Marked` and `Old` are GC traversal
state; they must not be overloaded to mean queued, finalizing, or immortal.

## Type-Level GC Tracking

Tracking is a property of exact object layout, expressed on the type object.

Tracked examples:

- type objects and heap types
- instances with instance dictionaries
- modules
- dicts, lists, tuples, slices, mapping proxies
- functions, methods, descriptors, code objects, frames, cells
- exceptions and tracebacks
- iterators that own referenced containers or callables

Untracked examples:

- exact int objects
- exact float objects
- bool, `None`, `NotImplemented`
- strings and bytes while they own no object references
- range objects and range iterators while they only store scalar state

Rules:

- A type whose instances can own `Object *` references must be tracked unless a
  test proves those references cannot participate in a cycle.
- A tracked object is inserted into the runtime tracked-object list at
  allocation time.
- Untracked objects are still heap objects and still obey refcount ownership.
  They are simply invisible to cycle tracing and do not increase cycle-GC
  pressure.
- If a type changes layout and starts owning object references, its tracking
  flag and tests must change in the same patch.

## Reference Ownership

Reference counts are the primary lifetime mechanism.

Strong owners:

- VM value stack entries
- fast locals
- frame fields
- cells
- containers
- functions, methods, descriptors, modules, code objects, traceback objects
- exception state
- runtime roots
- temporary roots
- C extension handles

Borrowed references are allowed only inside a lexical region where:

- the owner is known to outlive the borrow;
- no allocation can trigger GC;
- no Python callback can run;
- no container mutation can remove the borrowed object;
- no thread transition can hide the owner from GC.

If any of those conditions is false, the code must take a strong reference or a
temporary root.

The VM stack must not be a trace-only root forever. It must have a clear
ownership contract. The target contract is:

- `Frame::push_owned(Object *)` consumes an owned reference.
- `Frame::push_borrowed(Object *)` increments before storing.
- `Frame::pop_owned()` returns an owned reference.
- `Frame::discard()` decrements.
- Existing generic `push()`/`pop()` should be migrated only after opcode stack
  effects are audited and tested.

This avoids hiding ownership bugs behind tracing roots while still allowing a
controlled migration.

## Atomic Refcount Semantics

`incref()`:

- Uses relaxed atomic increment for the common path.
- Requires the caller to already hold a live reference or a published root.
- Must not resurrect an object with `QueuedForDealloc` or `Finalizing` set.

`decref()`:

- Uses release decrement.
- If the previous value was greater than one, returns immediately.
- If the previous value was one, performs an acquire fence, marks
  `QueuedForDealloc`, and enqueues the object for deferred destruction.
- Must not clear references or delete storage inline.

Debug builds should detect:

- decrement from zero;
- incref of queued/finalizing objects outside an explicit resurrection protocol;
- double enqueue;
- deallocation of objects still present in roots, frames, containers, or tracked
  lists.

## Deferred Deallocation

No-GIL `decref()` must be non-recursive. It only schedules cleanup.

Queues:

- Each `ThreadState` has a local deferred-deallocation queue.
- The runtime has a global deferred-deallocation queue for code without an
  active `ThreadState`.
- Queue nodes are object pointers whose `QueuedForDealloc` bit is already set.

Drain points:

- VM eval breaker
- before stop-the-world cycle GC marks roots
- after cycle GC clears unreachable cycles
- thread exit
- returning from native calls into the VM
- allocation pressure slow path
- runtime shutdown

Drain protocol:

1. Pop queued objects.
2. Skip if refcount is no longer zero. Clear `QueuedForDealloc` only under the
   documented resurrection path.
3. Set `Finalizing`.
4. Call `clear_references()`.
5. Child references that reach zero are queued, not destroyed recursively.
6. Remove the object from heap/tracked storage.
7. Delete the object storage.

The drain loop has a work budget during normal execution. It can run to
completion at thread exit, explicit GC, and runtime shutdown.

## Cycle GC

Cycle GC is a stop-the-world collector over tracked objects only.

It must not be responsible for ordinary zero-ref leaf cleanup. Explicit
`gc.collect()` should collect cycles and drain pending deallocations, not scan
millions of numeric temporaries that should already have been released through
refcount ownership.

Collection protocol:

1. Drain deferred deallocation queues briefly to remove obvious zero-ref
   garbage before marking.
2. Publish a new GC stop request epoch.
3. Running VM threads poll the eval breaker and park at opcode safepoints.
4. Threads blocked in native waits enter `NativeWait` only after publishing all
   roots and proving no unrooted temporaries are live.
5. The collector waits until every registered active thread is parked,
   detached, or in stable native wait.
6. Mark roots.
7. Traverse only tracked children.
8. For young collection, treat old tracked objects as roots unless an
   remembered-set implementation is present.
9. For full collection, inspect all tracked objects.
10. Clear unreachable tracked cycles.
11. Drain deallocation queues again.
12. Unmark survivors, update generations, clear the stop request, and wake
    parked threads.

Reference counts alone are not GC roots. A non-zero refcount can be internal to
an unreachable cycle.

## Root Set

The root set includes:

- runtime roots and singletons
- module table
- import machinery state
- registered thread frames
- VM value stacks
- fast locals
- block/exception state
- active traceback state
- temporary roots
- C extension handles
- stable native-wait roots

The root set excludes:

- arbitrary non-zero refcount objects
- untracked leaf objects not reachable from an explicit root
- stale pointers in cleared frames, popped stacks, or removed containers

No unrooted GC-managed object may be live across allocation, Python calls,
container operations that can allocate, or GC safepoints.

## Thread States

Every OS thread executing Python or runtime object code must have a registered
`ThreadState` before it can allocate, call Python, mutate containers, or hold
temporary roots.

Thread states:

- `Running`: executing Python or runtime code and polling eval breaker.
- `OpcodeSafepoint`: parked with frames and temporaries visible to GC.
- `NativeWait`: blocked in native code with stable published roots and no
  unrooted temporaries.
- `Detached`: not executing Python and not participating in GC coordination.
- `Finalizing`: draining thread-local queues and unregistering.

Thread exit protocol:

1. Stop bytecode execution.
2. Pop or clear all frames.
3. Clear handled exceptions and trace state.
4. Drain the thread-local deferred-deallocation queue.
5. Publish `Finalizing`.
6. Unregister from the runtime.
7. Notify GC coordination.

A thread with frame depth zero can be ignored by cycle-GC stop waiting only if
it has no unrooted temporaries and no active runtime mutation. That condition
must be explicit, not inferred from frame depth alone.

## Safepoints and Eval Breaker

The eval breaker handles:

- pending GC stop requests
- pending automatic GC requests
- deferred deallocation queue pressure
- asynchronous thread events

Safepoint requirements:

- The current frame stack is internally consistent.
- Stack effects for the current opcode are complete.
- All newly allocated objects are rooted, owned, or queued for destruction.
- No container mutation lock is held.
- No borrowed reference can be invalidated by the collector.

Long-running C++ loops that execute runtime operations must poll the same
breaker or enter a documented native-wait state.

## Container Safety

Without a GIL, containers protect themselves.

List rules:

- Structural mutation locks the list.
- Reads that inspect size and element pointer together lock the list or use a
  versioned snapshot.
- Iteration uses snapshot or fail-fast version checks.
- `clear_references()` must exchange the internal storage under lock, then
  decref outside the lock when possible.

Dict rules:

- Table mutation locks the dict.
- Generic hash and equality are evaluated outside the table lock because they
  can call Python.
- Exact builtin key fast paths may run under the table lock only if they cannot
  allocate or call Python.
- Iteration uses snapshots or versioned fail-fast behavior.
- Resize and rehash cannot expose partially initialized table state.

General rule:

- Never call Python code, allocate, collect, or invoke arbitrary descriptor,
  hash, equality, repr, or finalization logic while holding a container mutation
  lock unless the path is proven non-reentrant and documented.

## Lock Ordering

When locks must be nested, use this order:

1. GC coordination mutex
2. thread-state registry mutex
3. deallocation queue mutexes
4. runtime roots mutex
5. object/container mutexes
6. heap/tracked-storage mutex

Avoid holding heap storage locks while calling object methods. Heap locks should
protect membership and storage movement, not arbitrary runtime behavior.

## Runtime Storage

The runtime owns two logical object indexes:

- heap storage: all heap-allocated objects
- tracked storage: only objects visible to cycle GC

Invariants:

- Every tracked object appears in heap storage.
- No untracked object appears in tracked storage.
- Removing an object from heap storage removes it from tracked storage first or
  rebuilds tracked storage before the next collection.
- Object storage is deleted only by the deferred-deallocation drain or runtime
  shutdown, not by arbitrary `decref()` callers.

The implementation may initially keep heap storage as `vector<unique_ptr>`, but
the design should allow a future arena or slab allocator without changing
ownership semantics.

## Immortal Objects

Immortal objects include:

- `None`, `True`, `False`, `NotImplemented`
- small integer cache entries
- core built-in static type objects after bootstrap
- process/runtime-lifetime interned constants if introduced later

Rules:

- `decref()` on an immortal object is a no-op or debug-checked metadata update,
  but never queues destruction.
- Immortal objects can be roots for traversal if tracked.
- Immortality is not a substitute for rooting newly allocated temporaries.

## Finalization and Resurrection

The first implementation should not support arbitrary Python-level finalizers.
When finalizers are added:

- `Finalizing` prevents re-entrant clear.
- Resurrection is allowed only through a documented path that restores a strong
  refcount and clears `QueuedForDealloc`.
- Objects resurrected after partial clear must be in a valid documented state.
- Finalizers must not run while the collector holds heap or container locks.

Until those rules are implemented, finalizer-capable features must remain
limited and tested.

## Runtime Shutdown

Shutdown is a separate mode, not a normal GC cycle.

Shutdown protocol:

1. Stop new Python execution.
2. Detach or join all Python threads.
3. Drain all deferred-deallocation queues.
4. Clear runtime roots.
5. Clear modules and import state.
6. Drain again.
7. Delete remaining heap storage in a mode that prevents recursive refcount
   queues from accessing torn-down runtime state.

Shutdown tests must cover modules, exceptions, frames, containers, and thread
objects.

## Migration Plan

Implement this design in vertical slices:

1. Keep the existing GC stop coordination stable and tested.
2. Add lifecycle state bits: tracked, finalizing, queued, immortal.
3. Add explicit type tracking flags and tests for tracked vs untracked
   allocation pressure.
4. Introduce per-thread and runtime deferred-deallocation queues.
5. Change `decref()` from advisory decrement to enqueue-on-zero.
6. Add bounded queue draining at eval breaker, allocation slow path, explicit
   GC, and thread exit.
7. Make cycle GC traverse tracked storage instead of full heap storage.
8. Audit and migrate VM stack ownership opcode by opcode.
9. Audit containers for no-GIL locking, hash/equality reentrancy, and iteration
   versioning.
10. Add stress tests for GC while threads start, wait, exit, mutate containers,
    and run explicit `gc.collect()`.

Do not combine all slices in one patch. The dangerous boundary is switching
`decref()` semantics; it must land with queue draining, shutdown handling, and
tests.

## Required Tests

Focused tests:

- zero-ref objects are reclaimed without explicit `gc.collect()`;
- decref from one to zero queues exactly once;
- draining a queue clears child references without recursive deletion;
- cycles are collected even with positive internal refcounts;
- untracked leaf objects do not increase cycle-GC pressure;
- tracked containers do increase cycle-GC pressure;
- explicit `gc.collect()` drains queues and collects cycles;
- thread exit drains local queues;
- GC waits for running threads and ignores detached threads only when safe;
- native wait state publishes stable roots;
- dict/list concurrent mutation does not corrupt internal state.

Stress tests:

- repeated `gc.collect()` from multiple Python threads;
- allocating numeric temporaries in multiple threads;
- creating and dropping container cycles while other threads execute bytecode;
- thread start/exit racing with explicit GC;
- container mutation while iterators exist.

Performance gates:

- `test2.py multi 1000000` with explicit `gc.collect()` must not deadlock.
- Median `test2.py multi 1000000` must be at least competitive with CPython on
  the same machine.
- Removing explicit `gc.collect()` from the benchmark must not hide a leak or
  deferred queue backlog.
- Full C++ and Python regression tests must pass.

## Current Implementation Gap

The current stable implementation already has useful pieces:

- explicit thread registration;
- temporary roots;
- stop-the-world GC coordination;
- eval-breaker GC polling;
- object-level locks for some containers;
- small integer cache.

The missing pieces are the core no-GIL lifetime model:

- `decref()` does not reclaim or enqueue zero-ref objects;
- VM stack ownership is not explicit enough;
- cycle GC scans heap storage instead of tracked storage;
- leaf allocations create GC pressure;
- deferred deallocation queues do not exist;
- thread-state participation is not rich enough to prove all non-frame runtime
  activity is safe during collection.

Those gaps explain both instability and performance loss. The fix is not to
special-case the prime benchmark; it is to move ordinary temporary cleanup back
to refcount ownership and reserve GC for cycles.
