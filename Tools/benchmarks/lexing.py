#!/usr/bin/env python3
"""Measure public tokenization on growing, non-ASCII single lines."""
import argparse
import json
from pathlib import Path
import resource
import subprocess
import sys
import time

KERNEL = '''
import io
import sys
import tokenize
size = int(sys.argv[1])
source = '\\u03c0=0;' * size + '\\n'
count = sum(1 for item in tokenize.generate_tokens(io.StringIO(source).readline))
assert count == 4 * size + 2
print(count)
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--reference', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--runs', type=int, default=2)
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    manifest = {'kernel': KERNEL, 'includes_startup_and_import': True, 'commands': []}
    for label, binary in (('cxx', args.binary.resolve()), ('cpython', args.reference.resolve())):
        for size in (5000, 10000):
            inputs = []
            for sample in range(args.runs):
                command = [str(binary), '-c', KERNEL, str(size)]
                before = resource.getrusage(resource.RUSAGE_CHILDREN)
                start = time.perf_counter_ns()
                result = subprocess.run(command, capture_output=True, timeout=30)
                wall = time.perf_counter_ns() - start
                after = resource.getrusage(resource.RUSAGE_CHILDREN)
                cpu = round((after.ru_utime + after.ru_stime - before.ru_utime - before.ru_stime) * 1e9)
                if result.returncode or result.stdout != f'{4 * size + 2}\n'.encode() or result.stderr:
                    raise RuntimeError(f'{label}: {result.returncode}: {result.stdout!r} {result.stderr!r}')
                raw = args.output_dir / f'{label}-{size}-{sample}.txt'
                raw.write_text(f'tokenize_workers=1 characters={4 * size + 1} tokens={4 * size + 2} '
                               f'wall_ns={wall} process_cpu_ns={cpu}\n')
                inputs.append(raw)
                manifest['commands'].append({'command': command, 'raw': str(raw)})
            command = [sys.executable, str(Path(__file__).with_name('record.py')),
                       '--series', f'tokenize-{label}-{size}', '--output-dir', str(args.output_dir)]
            for raw in inputs:
                command.extend(['--input', str(raw)])
            subprocess.run(command, check=True)
    (args.output_dir / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')


if __name__ == '__main__':
    main()
