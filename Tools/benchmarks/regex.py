#!/usr/bin/env python3
"""Record complete-process scaling of the same shared-pattern regex workload."""
import argparse
import hashlib
import json
from pathlib import Path
import resource
import subprocess
import sys
import time

KERNEL = '''
import _thread
import re
import sys
size, workers = int(sys.argv[1]), int(sys.argv[2])
pattern = re.compile('a*$')
text = 'a' * size
gate = _thread.allocate_lock()
gate.acquire()
done = [_thread.allocate_lock() for _ in range(workers)]
for lock in done:
    lock.acquire()
results = [False] * workers
def worker(index):
    try:
        gate.acquire()
        gate.release()
        for _ in range(128 // workers):
            assert pattern.fullmatch(text).end() == size
        results[index] = True
    except BaseException:
        results[index] = False
    finally:
        done[index].release()
for index in range(workers):
    _thread.start_new_thread(worker, (index,))
gate.release()
for lock in done:
    lock.acquire()
assert all(results)
print(workers)
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--reference', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--runs', type=int, default=2)
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    manifest = {'kernel': KERNEL, 'kernel_sha256': hashlib.sha256(KERNEL.encode()).hexdigest(),
                'includes_startup_and_import': True, 'iterations': 128, 'commands': []}
    record = Path(__file__).with_name('record.py')
    for label, binary in (('cxx', args.binary.resolve()), ('cpython', args.reference.resolve())):
        digest = hashlib.sha256(binary.read_bytes()).hexdigest()
        for size in (2000000, 4000000):
            inputs = []
            for workers in (1, 2):
                for sample in range(args.runs):
                    command = [str(binary), '-c', KERNEL, str(size), str(workers)]
                    before = resource.getrusage(resource.RUSAGE_CHILDREN)
                    start = time.perf_counter_ns()
                    result = subprocess.run(command, capture_output=True, timeout=30)
                    wall = time.perf_counter_ns() - start
                    after = resource.getrusage(resource.RUSAGE_CHILDREN)
                    cpu = round((after.ru_utime + after.ru_stime - before.ru_utime - before.ru_stime) * 1e9)
                    if result.returncode or result.stdout != f'{workers}\n'.encode() or result.stderr:
                        raise RuntimeError(f'{label}: {result.returncode}: {result.stdout!r} {result.stderr!r}')
                    raw = args.output_dir / f'{label}-{size}-{workers}-{sample}.txt'
                    raw.write_text(f'regex_fullmatch_workers={workers} characters={size} '
                                   f'wall_ns={wall} process_cpu_ns={cpu}\n')
                    inputs.append(raw)
                    manifest['commands'].append({'command': command, 'binary_sha256': digest,
                                                 'raw': str(raw), 'exit_code': result.returncode})
            command = [sys.executable, str(record), '--series', f'regex-{label}-{size}',
                       '--output-dir', str(args.output_dir)]
            for raw in inputs:
                command.extend(['--input', str(raw)])
            subprocess.run(command, check=True)
    (args.output_dir / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')


if __name__ == '__main__':
    main()
