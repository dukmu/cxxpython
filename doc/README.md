# cxxpython Documentation Index

This directory records the restart plan for the new `cxxpython` repository.

Active authority: [foundation redesign plan](../cxxpython_foundation_redesign_plan.md).
[Foundation contracts and migration ledger](foundation_contracts.md) records
new-core decisions, tests and remaining gates. The older architecture, feature
matrix, development plan and no-GIL notes below describe the legacy runtime;
they do not override the foundation redesign.

- [经验报告](experience_report.md): lessons from the previous implementation.
- [Python 特性支持目标](python_feature_matrix.md): target support matrix.
- [架构设计](architecture_design.md): runtime, compiler, VM, modules, GC, C/C++ API.
- [开发指南](development_guide.md): engineering rules and TDD workflow.
- [开发路线图](development_plan.md): milestone plan and acceptance criteria.
- [底层基建设计](bottom_substrate_design.md): execution, class construction,
  import, closures, slice, buffer, IO, and traceback substrate plan.
- [执行内省与并发设计](execution_introspection_design.md): traceback,
  lexer/parser script exposure, `dis`/`pdb` preparation, and the initial
  threading/coroutine/process model.
- [无 GIL GC 与多线程设计](nogil_gc_threading_design.md): target ownership,
  deferred deallocation, stop-the-world cycle GC, thread states, and locking
  invariants for no-GIL execution.

The goal is not to preserve the old implementation. The goal is to preserve
hard-earned knowledge while designing a cleaner, more complete Python 3 runtime
from first principles.
