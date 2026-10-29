# cxxpython Documentation Index

Active authority: [Plan 2](../cxxpython_foundation_redesign_plan_2.md).

- [Current architecture](architecture_design.md): CPython-shaped source layout
  and the single current runtime/compiler/VM path.
- [Development guide](development_guide.md): build, tests, clang-format and evidence.
- [Current runtime contracts](foundation_contracts.md): ownership, GC/threads,
  publication, calls, closure execution and actual capability boundaries.
- [Development history](development_status.md): append/prepend-only progress.
- [Benchmark evidence](benchmarks/): retained raw observations and generated reports.
- [Lessons](experience_report.md): earlier engineering experience.

The legacy implementation, its standard library, extension API and tests have
been removed. `archive/` preserves old design documents and the superseded
contract ledger as historical references, not active implementation contracts or
claims about current support. Removal does not waive the full Plan 2 gates.
