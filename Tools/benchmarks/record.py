#!/usr/bin/env python3
"""Run fixed-work Python benchmarks, retain raw evidence, export standard pyperf JSON."""

import argparse
import ast
import csv
from datetime import datetime, timezone
import json
import math
import os
from pathlib import Path
import platform
import re
import resource
import signal
import shutil
import statistics
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
WORKER = Path(__file__).with_name('workloads.py')


def catalog():
    if __package__:
        from .workloads import CASES
    else:
        from workloads import CASES
    return {**CASES, 'startup': {'group': 'startup', 'size': 1,
                               'description': 'Fresh process: interpreter -c pass'}}


def positive(text):
    value = int(text)
    if value < 1:
        raise argparse.ArgumentTypeError('must be positive')
    return value


def parse_runtimes(items):
    result = {}
    for item in items:
        label, separator, path = item.partition('=')
        if not separator or not re.fullmatch(r'[A-Za-z0-9_.-]+', label) or label in result:
            raise ValueError('--python requires unique LABEL=EXECUTABLE entries')
        executable = os.path.abspath(shutil.which(path) or path)
        if not Path(executable).is_file():
            raise ValueError('interpreter not found: ' + executable)
        result[label] = executable
    return result


def decode_payload(text):
    # The target needs no json/subprocess/pyperf library. Only the host parses literals.
    value = ast.literal_eval(text.strip())
    if not isinstance(value, dict):
        raise ValueError('worker did not return a dictionary')
    return value


def validate_payload(payload, expected, warmups, values):
    if payload.get('check') != 'ok' or any(payload.get(key) != value for key, value in expected.items()):
        raise ValueError('worker scenario or correctness check differs')
    python = payload.get('python')
    if (not isinstance(payload.get('group'), str) or not isinstance(python, dict)
            or any(not isinstance(python.get(key), str) for key in ('version', 'executable'))):
        raise ValueError('invalid worker metadata')
    for name, count in [('warmups', warmups), ('values', values)]:
        samples = payload.get(name)
        if not isinstance(samples, list) or len(samples) != count:
            raise ValueError('worker sample count differs: ' + name)
        for sample in samples:
            if not isinstance(sample, dict):
                raise ValueError('invalid worker sample')
            for key, minimum in [('wall_ns', 1), ('process_cpu_ns', 0)]:
                if type(sample.get(key)) is not int or sample[key] < minimum:
                    raise ValueError('invalid worker clock: ' + key)


def append_record(path, record):
    with path.open('a', encoding='utf-8') as stream:
        stream.write(json.dumps(record, ensure_ascii=True) + '\n')


def capture(command, cwd, timeout, raw, runtime, process, kind, expected=None, warmups=0, values=1):
    started = datetime.now(timezone.utc).isoformat()
    before = resource.getrusage(resource.RUSAGE_CHILDREN)
    start = time.perf_counter_ns()
    error = None
    try:
        with subprocess.Popen(command, cwd=cwd, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE, text=True, start_new_session=True) as child:
            try:
                stdout, stderr = child.communicate(timeout=timeout)
            except BaseException as exc:
                # perf launches a target child: kill the session, not just perf.
                try:
                    os.killpg(child.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                stdout, stderr = child.communicate()
                if not isinstance(exc, subprocess.TimeoutExpired):
                    raise
                error = 'timeout after %g seconds' % timeout
            status = child.returncode
    except OSError as exc:
        stdout, stderr, status, error = '', '', None, str(exc)
    duration = time.perf_counter_ns() - start
    after = resource.getrusage(resource.RUSAGE_CHILDREN)
    if isinstance(stdout, bytes):
        stdout = stdout.decode('utf-8', 'replace')
    if isinstance(stderr, bytes):
        stderr = stderr.decode('utf-8', 'replace')
    payload = None
    if not error:
        if status != 0 or (stderr and kind != 'profile'):
            error = 'worker failed or wrote unexpected stderr'
        elif kind == 'startup_value':
            if stdout:
                error = 'startup command wrote unexpected output'
        else:
            try:
                payload = decode_payload(stdout)
                if expected:
                    validate_payload(payload, expected, warmups, values)
            except (SyntaxError, ValueError, TypeError) as exc:
                error = str(exc)
    record = dict(kind=kind, runtime=runtime, process=process, command=command,
                  cwd=str(cwd), started_at=started, duration_ns=duration, stdout=stdout,
                  stderr=stderr, returncode=status, error=error, payload=payload,
                  process_cpu_ns=round((after.ru_utime + after.ru_stime - before.ru_utime - before.ru_stime) * 1e9))
    append_record(raw, record)
    if error:
        raise RuntimeError('%s: %s; diagnostics: %s' % (runtime, error, raw))
    return record


def calibrated_loops(durations, minimum):
    needed = max(1, math.ceil(minimum * 1e9 / min(durations)))
    return 1 << (needed - 1).bit_length()


def sample_rows(records):
    rows = []
    for record in records:
        if record['kind'] != 'run' or record.get('error') or record.get('returncode') != 0:
            continue
        data = record['payload']
        for index, value in enumerate(data['values']):
            rows.append(dict(runtime=record['runtime'], case=data['case'], group=data['group'],
                             size=data['size'], workers=data['workers'], mode=data.get('mode', 'kernel'),
                             process=record['process'], sample=index, loops=data['loops'],
                             wall_ns=value['wall_ns'] / data['loops'],
                             process_cpu_ns=value['process_cpu_ns'] / data['loops']))
    return rows


def render_report(records, reference=None):
    groups = {}
    for row in sample_rows(records):
        key = tuple(row[name] for name in ('runtime', 'case', 'size', 'workers', 'mode'))
        groups.setdefault(key, []).append(row)
    medians = {key: statistics.median(row['wall_ns'] for row in rows) for key, rows in groups.items()}
    failed = sum(bool(record.get('error')) for record in records)
    completed = sum(record['kind'] == 'run' and not record.get('error') for record in records)
    planned = sum(record.get('planned_runs', 0) for record in records if record['kind'] == 'header')
    lines = ['# Python Benchmark Report', '', 'Generated by Tools/benchmarks/record.py; do not edit tables.', '',
             'Completed runs: %d. Planned runs: %s. Failed processes: %d.' % (completed, planned or 'unknown', failed), '',
             'Kernel times exclude preparation and result validation; startup is complete process time.',
             'Values are normalized per fixed-work batch. Warmups/calibration/failures are excluded, not discarded from raw data.',
             'CPU/wall is utilization, not speedup. Speedup uses the same runtime/size at one worker.', '',
             '| Runtime | Case | Size | Workers | Mode | Samples | Median ms | Min ms | Max ms | RSD % | CPU/wall | Speedup | /Reference |',
             '| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |']
    warnings = []
    snapshots = [record['sources'] for record in records if record['kind'] == 'header']
    if snapshots and any(snapshot != snapshots[0] for snapshot in snapshots[1:]):
        warnings.append('Workload source snapshots differ; audit that compared cases still perform the same work.')
    for key, rows in sorted(groups.items()):
        runtime, case, size, workers, mode = key
        values = [row['wall_ns'] for row in rows]
        wall = medians[key]
        rsd = statistics.stdev(values) / statistics.mean(values) if len(values) > 1 else 0
        cpu = statistics.median(row['process_cpu_ns'] for row in rows) / wall
        baseline = medians.get((runtime, case, size, 1, mode))
        comparison = medians.get((reference, case, size, workers, mode))
        lines.append('| %s | %s | %d | %d | %s | %d | %.3f | %.3f | %.3f | %.1f | %.3f | %s | %s |' %
                     (runtime, case, size, workers, mode, len(values), wall / 1e6, min(values) / 1e6,
                      max(values) / 1e6, rsd * 100, cpu,
                      '%.3f' % (baseline / wall) if baseline else '-',
                      '%.3f' % (wall / comparison) if comparison else '-'))
        if rsd > 0.1:
            warnings.append('%s/%s size=%d workers=%d: high variation; inspect raw values.' % key[:4])
        if rows[0]['group'] == 'cpu' and cpu < 0.8:
            warnings.append('%s/%s size=%d workers=%d: low CPU/wall; investigate waits, scheduling or CPU accounting.' % key[:4])
    lines += ['', '## Diagnostics', ''] + (warnings or ['No configured variation/utilization warnings. This is not a statistical significance claim.'])
    lines += ['', '## GC Observations', '',
              'Whole worker, including warmups and validation. Native pause fields are last-event/lifetime-max snapshots, not pause totals or percentiles.', '',
              '| Runtime | Case | Size | Workers | Process | Collections | Last STW ns | Last rendezvous ns | Max rendezvous ns |',
              '| --- | --- | --- | --- | --- | --- | --- | --- | --- |']
    observed = []
    for record in records:
        if record['kind'] != 'run' or record.get('error'):
            continue
        data = record['payload']
        if 'gc_before' not in data:
            continue
        before, after = data['gc_before'], data['gc_after']
        count = sum(item['collections'] for item in after['stats']) - sum(item['collections'] for item in before['stats'])
        heap = after.get('heap_stats', {})
        if 'heap_stats' in before and heap:
            count = heap['collections'] - before['heap_stats']['collections']
        observed.append((record['runtime'], data['case'], data['size'], data['workers'], record['process'], count,
                         heap.get('last_pause_ns', '-') if count else '-',
                         heap.get('last_rendezvous_ns', '-') if count else '-',
                         heap.get('max_rendezvous_ns', '-') if count else '-'))
    lines += ['| ' + ' | '.join(map(str, row)) + ' |' for row in sorted(observed)]
    return '\n'.join(lines) + '\n'


def require_pyperf():
    try:
        import pyperf
    except ModuleNotFoundError as exc:
        raise RuntimeError('host dependency missing: install Tools/benchmarks/requirements.txt') from exc
    return pyperf


def export_pyperf(records, output):
    pyperf = require_pyperf()
    groups = {}
    for record in records:
        if record['kind'] != 'run' or record.get('error') or record.get('returncode') != 0:
            continue
        data = record['payload']
        name = '%s[size=%d,workers=%d,%s]' % (data['case'], data['size'], data['workers'], data.get('mode', 'kernel'))
        metadata = dict(name=name, loops=data['loops'], unit='second',
                        duration=record['duration_ns'] / 1e9,
                        python_version=data['python']['version'],
                        python_executable=data['python']['executable'])
        if 'implementation' in data['python']:
            metadata['python_implementation'] = data['python']['implementation']
        values = [value['wall_ns'] / data['loops'] / 1e9 for value in data['values']]
        warmups = [(data['loops'], value['wall_ns'] / data['loops'] / 1e9) for value in data['warmups']]
        run = pyperf.Run(values, warmups=warmups, metadata=metadata, collect_metadata=False)
        groups.setdefault(record['runtime'], {}).setdefault(name, []).append(run)
    for runtime, benchmarks in sorted(groups.items()):
        suite = pyperf.BenchmarkSuite([pyperf.Benchmark(runs) for name, runs in sorted(benchmarks.items())])
        suite.dump(str(output / (runtime + '.pyperf.json')), replace=True)


def read_records(paths):
    records = []
    for path in paths:
        for line in path.read_text(encoding='utf-8').splitlines():
            item = json.loads(line)
            if item['kind'] == 'header':
                if item['schema'] != 1:
                    raise ValueError('unsupported benchmark schema')
            records.append(item)
    return records


def write_outputs(records, output, reference):
    rows = sample_rows(records)
    with (output / 'samples.csv').open('w', newline='', encoding='utf-8') as stream:
        fields = ['runtime', 'case', 'group', 'size', 'workers', 'mode', 'process', 'sample', 'loops', 'wall_ns', 'process_cpu_ns']
        writer = csv.DictWriter(stream, fields, lineterminator='\n')
        writer.writeheader()
        writer.writerows(rows)
    (output / 'report.md').write_text(render_report(records, reference), encoding='utf-8')
    if rows:
        export_pyperf(records, output)


def worker_command(executable, case, size, workers, loops, warmups, values, matrix):
    command = [executable, str(WORKER), '--case', case, '--size', str(size), '--workers', str(workers),
               '--loops', str(loops), '--warmups', str(warmups), '--values', str(values)]
    if case == 'matrix':
        command += ['--matrix', str(matrix)]
    return command


def header(runtimes, cases, workers, processes, matrix):
    sources = {name: WORKER.with_name(name).read_text(encoding='utf-8')
               for name in ('workloads.py', 'nbody.py', 'fannkuch.py')}
    if matrix:
        sources['external_matrix'] = matrix.read_text(encoding='utf-8')
    revision = subprocess.run(['git', 'describe', '--always', '--dirty'], cwd=ROOT, capture_output=True, text=True)
    binaries = {}
    for label, path in runtimes.items():
        version = subprocess.run([path, '--version'], capture_output=True, text=True, timeout=30)
        if version.returncode:
            raise RuntimeError('interpreter version query failed: ' + path)
        cache = Path(path).parent / 'CMakeCache.txt'
        build = dict(line.split('=', 1) for line in cache.read_text().splitlines()
                     if line.startswith(('CMAKE_BUILD_TYPE:', 'CMAKE_CXX_COMPILER:', 'CMAKE_CXX_FLAGS:', 'CMAKE_CXX_FLAGS_RELEASE:'))) if cache.exists() else {}
        binaries[label] = dict(executable=path, version=(version.stdout + version.stderr).strip(),
                               mtime_ns=Path(path).stat().st_mtime_ns, build=build)
    return dict(kind='header', schema=1, sources=sources, runtimes=binaries,
                harness_revision=revision.stdout.strip(), platform=platform.platform(),
                logical_cpus=os.cpu_count(), affinity=sorted(os.sched_getaffinity(0)) if hasattr(os, 'sched_getaffinity') else None,
                command=sys.argv, started_at=datetime.now(timezone.utc).isoformat(),
                planned_runs=len(runtimes) * processes * sum(1 if case == 'startup' else len(workers) for case in cases))


def run_suite(args):
    require_pyperf()
    runtimes = parse_runtimes(args.python)
    workers = sorted(set(positive(item) for item in args.workers.split(',')))
    specs = catalog()
    cases = args.case or [name for name, spec in specs.items() if name != 'matrix' and (not args.group or spec['group'] in args.group)]
    if args.reference and args.reference not in runtimes:
        raise ValueError('--reference must name one of the supplied interpreter labels')
    matrix = args.matrix.resolve() if args.matrix else None
    if 'matrix' in cases and not matrix:
        raise ValueError('--case matrix requires --matrix PATH')
    if not math.isfinite(args.min_time) or args.min_time <= 0:
        raise ValueError('--min-time must be positive and finite')
    output = args.output or ROOT / '.cache/benchmarks' / datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    output.mkdir(parents=True, exist_ok=True)
    raw = output / 'runs.jsonl'
    if raw.exists():
        raise ValueError('raw run file already exists; choose a new --output directory')
    info = header(runtimes, cases, workers, args.processes, matrix)
    append_record(raw, info)
    loop_counts = {}
    try:
        for case in cases:
            size = 1 if case == 'startup' else args.size or specs[case]['size']
            for process in range(args.processes):
                labels = list(runtimes)
                if process % 2:
                    labels.reverse()
                for runtime in labels:
                    executable = runtimes[runtime]
                    if case == 'startup':
                        samples = []
                        for sample in range(args.warmups + args.values):
                            child = capture([executable, '-c', 'pass'], ROOT, args.timeout, raw, runtime, process, 'startup_value')
                            samples.append(dict(wall_ns=child['duration_ns'], process_cpu_ns=child['process_cpu_ns']))
                        payload = dict(case=case, group='startup', size=1, workers=1, loops=1, mode='process', check='ok',
                                       warmups=samples[:args.warmups], values=samples[args.warmups:],
                                       python=dict(version=info['runtimes'][runtime]['version'], executable=executable))
                        append_record(raw, dict(kind='run', runtime=runtime, process=process, returncode=0, error=None,
                                                duration_ns=sum(s['wall_ns'] for s in samples), payload=payload))
                        continue
                    key = runtime, case, size
                    if key not in loop_counts:
                        loops = args.loops
                        if loops is None:
                            durations = []
                            for count in workers:
                                command = worker_command(executable, case, size, count, 1, 1, 1, matrix)
                                with tempfile.TemporaryDirectory(prefix='cxx-bench-') as scratch:
                                    probe = capture(command, Path(scratch), args.timeout, raw, runtime, process, 'calibration',
                                                    dict(case=case, size=size, workers=count, loops=1), 1, 1)
                                durations.append(probe['payload']['values'][0]['wall_ns'])
                            loops = calibrated_loops(durations, args.min_time)
                        loop_counts[key] = loops
                    for count in workers:
                        loops = loop_counts[key]
                        print('%s %s size=%d workers=%d process=%d loops=%d' % (runtime, case, size, count, process, loops), flush=True)
                        command = worker_command(executable, case, size, count, loops, args.warmups, args.values, matrix)
                        with tempfile.TemporaryDirectory(prefix='cxx-bench-') as scratch:
                            capture(command, Path(scratch), args.timeout, raw, runtime, process, 'run',
                                    dict(case=case, size=size, workers=count, loops=loops), args.warmups, args.values)
    finally:
        write_outputs(read_records([raw]), output, args.reference)
    print(output / 'report.md')


def profile(args):
    runtime = parse_runtimes([args.python])
    label, executable = next(iter(runtime.items()))
    if args.case == 'matrix' and not args.matrix:
        raise ValueError('--case matrix requires --matrix PATH')
    output = args.output
    output.mkdir(parents=True, exist_ok=True)
    raw = output / 'profile.jsonl'
    if raw.exists():
        raise ValueError('profile already exists; choose a new --output directory')
    matrix = args.matrix.resolve() if args.matrix else None
    size = args.size or catalog()[args.case]['size']
    append_record(raw, header(runtime, [args.case], [args.workers], 1, matrix))
    command = [args.perf, 'record', '-o', str((output / 'perf.data').resolve()), '-e', 'cpu-clock:u', '-F', '99', '-g', '--call-graph', 'dwarf', '--']
    command += worker_command(executable, args.case, size, args.workers, args.loops, 1, 1, matrix)
    with tempfile.TemporaryDirectory(prefix='cxx-profile-') as scratch:
        capture(command, Path(scratch), args.timeout, raw, label, 0, 'profile',
                dict(case=args.case, size=size, workers=args.workers, loops=args.loops), 1, 1)
    for name, options in [('self', ['--no-children', '--call-graph', 'none']),
                          ('callers', ['--children', '--call-graph', 'graph,0.5,caller'])]:
        result = subprocess.run([args.perf, 'report', '-i', str(output / 'perf.data'), '--stdio', '--percent-limit', '1', *options], capture_output=True, text=True)
        (output / (name + '.txt')).write_text(result.stdout + result.stderr, encoding='utf-8')
        if result.returncode:
            raise RuntimeError('perf report failed: ' + name)
    print(output / 'self.txt')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    sub.add_parser('list', help='list workloads and fixed-work size units')
    run = sub.add_parser('run', help='sequential calibrated/warmed independent-process suite')
    run.add_argument('--python', action='append', required=True, metavar='LABEL=EXECUTABLE')
    run.add_argument('--reference', help='interpreter label used as report denominator')
    run.add_argument('--case', action='append', choices=sorted(catalog()))
    run.add_argument('--group', action='append', choices=['cpu', 'gc', 'io', 'library', 'startup'])
    run.add_argument('--workers', default='1,2,4')
    run.add_argument('--size', type=positive, help='override the selected workload size (units shown by list)')
    run.add_argument('--processes', type=positive, default=3)
    run.add_argument('--values', type=positive, default=3)
    run.add_argument('--warmups', type=int, default=1)
    run.add_argument('--loops', type=positive, help='fixed loops; otherwise calibrate once per runtime/case/size')
    run.add_argument('--min-time', type=float, default=0.1)
    run.add_argument('--timeout', type=float, default=120)
    run.add_argument('--matrix', type=Path)
    run.add_argument('--output', type=Path)
    report = sub.add_parser('report', help='regenerate tables and pyperf JSON without executing targets')
    report.add_argument('--input', type=Path, action='append', required=True)
    report.add_argument('--reference')
    report.add_argument('--output', type=Path, required=True)
    perf = sub.add_parser('profile', help='separate Linux perf sampling; never a timed suite value')
    perf.add_argument('--python', required=True, metavar='LABEL=EXECUTABLE')
    perf.add_argument('--case', choices=sorted(set(catalog()) - {'startup'}), required=True)
    perf.add_argument('--size', type=positive)
    perf.add_argument('--workers', type=positive, default=1)
    perf.add_argument('--loops', type=positive, default=1)
    perf.add_argument('--matrix', type=Path)
    perf.add_argument('--perf', default='perf')
    perf.add_argument('--timeout', type=float, default=120)
    perf.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.command == 'list':
            for name, item in catalog().items():
                print('%-12s %-8s size=%-8d %s' % (name, item['group'], item['size'], item['description']))
        elif args.command == 'run':
            if args.warmups < 0:
                raise ValueError('--warmups must be nonnegative')
            run_suite(args)
        elif args.command == 'profile':
            profile(args)
        else:
            require_pyperf()
            records = read_records(args.input)
            args.output.mkdir(parents=True, exist_ok=True)
            write_outputs(records, args.output, args.reference)
    except (ValueError, RuntimeError, OSError) as exc:
        parser.exit(1, '%s\n' % exc)


if __name__ == '__main__':
    main()
