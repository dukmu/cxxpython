# Python Benchmarks

One host controller, one target worker, the same source on both interpreters.
The target needs no pyperf, subprocess or JSON package. It emits a Python literal
which the host reads with ast.literal_eval, then stores as JSON and exports using
the actual pyperf Run/Benchmark/Suite API. No custom JSON codec or native benchmark
interpreter is involved.

## Setup And Run

Use a host Python with venv/ensurepip, not cxxpython, for the controller. On hosts
without ensurepip, use an existing Python environment that provides it.

```sh
python3 -m venv .cache/benchmark-venv
.cache/benchmark-venv/bin/python -m pip install -r Tools/benchmarks/requirements.txt
.cache/benchmark-venv/bin/python Tools/benchmarks/record.py list
.cache/benchmark-venv/bin/python Tools/benchmarks/record.py run \
    --python cxx=build/cxxpython --python cpython=/path/to/python3.12.14 \
    --reference cpython --case nbody --workers 1,2,4 \
    --output .cache/benchmarks/nbody-baseline
```

Select cases with repeated `--case`, or groups with repeated `--group` (cpu, gc,
io, library, startup). Without a selection, all built-in cases run. `--size`
overrides the size unit shown by `list`; it is NOT iterations per thread.
Default parameters are 3 independent target processes, 1 warmup and 3 measured
values per process. These are development defaults, not a significance guarantee.
Use more processes/values when investigating small differences.

For each interpreter/case/size, calibration probes all requested worker counts
and chooses one loop count from the fastest probe, targeting 100 ms per value.
That SAME loop count is used for all worker counts and processes. `--min-time`
changes the target; `--loops` fixes it explicitly. Independent interpreters may
need different loop counts; results are normalized per complete fixed-work batch.
Interpreter order alternates between process rounds. Runs are always sequential.

Start timing only after builds, tests and other CPU-heavy work have stopped.
`--processes 1 --values 1 --warmups 0 --loops 1` is a correctness smoke setting,
not evidence of stable performance. Failed/timed-out workers retain diagnostics
and make the command fail; no favorable subset is selected. Variation and low
CPU utilization in CPU workloads are reported rather than silently removed.

## Workloads And Timing

- `nbody` and `fannkuch` adapt algorithms from fixed pyperformance 1.14.0 commit
  `2ab96f98f98274d07ebfdfd66e4f021723179706`; see source notices and
  `LICENSE.pyperformance`. Per-job parameters are 200 physics steps and n=7,
  not the upstream suite defaults. Size counts independent jobs. Mutable n-body
  state is private to each job. Do not publish these as default pyperformance scores.
- `allocate` creates short-lived containers with automatic GC enabled. `cycles`
  creates self-cycles and explicitly collects after each complete batch; only
  this declared collector workload forces collection.
- `dict_int` constructs, updates and deletes entries in independent 64-key integer
  dictionaries, then validates the remaining values by sum. Size counts complete
  dictionaries, not keys or jobs per thread. This exercises multi-entry/non-string
  storage alongside allocate's one-string-key dictionary; natural GC stays enabled.
- `file_io` creates/writes/reads/unlinks the same total number of files for every
  thread count. It measures filesystem/page-cache workflows, not durable storage
  or device throughput; it does not call fsync.
- `regex` shares one immutable compiled pattern/input; `tokenize` repeats fixed
  non-ASCII input. Both preserve total job count across threads.
- `startup` times fresh `interpreter -c pass` processes, including process launch
  and teardown. It is separate from inner-kernel measurements.

The other cases exclude source loading, preparation and result validation from
their kernel clocks. Each measured loop includes the selected job execution,
thread creation and actual joins. Threads execute in one Interpreter/shared heap;
job indices are partitioned, not duplicated. One worker executes directly.
CPU/wall is process CPU utilization, NOT speedup. Scaling uses the same runtime,
case and size at one worker; compare absolute times against the reference as well.
I/O concurrency alone is not evidence of parallel Python CPU execution.

GC snapshots cover the entire worker, including warmups and validation. The
optional native heap snapshot is absent on CPython, not fabricated. Last STW and
rendezvous values are last-event observations; max rendezvous is a lifetime maximum.
They are not cumulative pause time or a pause distribution. The collector is not
disabled to improve a score. If the counter did not advance, old setup pause
fields are not attributed to the measured worker window. Snapshot construction
itself can allocate. Short-lived-thread CPU accounting can differ by environment;
keep the reported clocks, inspect warnings and do not infer speedup from CPU/wall.

The user's local `matrix.py` is supported without copying it into this repository:

```sh
.cache/benchmark-venv/bin/python Tools/benchmarks/record.py run \
    --python cxx=build/cxxpython --python cpython=/path/to/python3.12.14 \
    --reference cpython --case matrix --matrix ./matrix.py --size 64 \
    --workers 1,2,4 --output .cache/benchmarks/matrix-baseline
```

The explicit trusted-source adapter uses the supplied `make_matrix` and
`matmul_mt` functions. Loading and serial-reference construction are outside the
timed region; row equality is checked after each call. Matrix is never selected
implicitly and the local source is never committed.

## Results And Profiling

Each output directory contains raw `runs.jsonl`, normalized `samples.csv`, generated
`report.md` and one standard `LABEL.pyperf.json` per interpreter. Raw data retain
calibration, warmups, failed stdout/stderr, complete commands, source snapshots,
binary paths/mtimes, reported versions and available CMake build flags. The harness
checkout revision is NOT proof of which source built an arbitrary supplied binary;
label builds explicitly and retain their build configuration. No binary checksums
or generated benchmark tables belong in source control. Source differences are
flagged when merging runs; audit that the selected cases still do the same work.
Formatting changes do not force new measurements or rewrite old source snapshots.

```sh
.cache/benchmark-venv/bin/python Tools/benchmarks/record.py report \
    --input .cache/benchmarks/matrix-baseline/runs.jsonl --reference cpython \
    --output .cache/benchmarks/matrix-baseline
.cache/benchmark-venv/bin/python -m pyperf compare_to \
    .cache/benchmarks/matrix-baseline/cpython.pyperf.json \
    .cache/benchmarks/matrix-baseline/cxx.pyperf.json --table
.cache/benchmark-venv/bin/python Tools/benchmarks/record.py profile \
    --python cxx=build/cxxpython --case nbody --workers 4 --loops 8 \
    --output .cache/benchmarks/nbody-profile
```

Profiling is a separate run, never added to timing tables. It retains perf.data,
self.txt, callers.txt and the exact command/worker diagnostics. It includes process
startup and warmup: increase loops to focus sampling on kernel work. Use
`--perf /path/to/working/perf` when the system wrapper does not match the kernel.
The default software event is cpu-clock:u. Hardware cache/cycle conclusions need
available PMU events, not just software CPU samples.

Methodology and file/API compatibility are based on the primary
[pyperf benchmark guide](https://pyperf.readthedocs.io/en/latest/run_benchmark.html),
[pyperf API](https://pyperf.readthedocs.io/en/latest/api.html) and
[pyperformance workloads](https://pyperformance.readthedocs.io/benchmarks.html).
