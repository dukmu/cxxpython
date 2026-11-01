"""Run an unchanged dill distribution's own scripts with the product interpreter."""

import argparse
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True, type=Path)
    parser.add_argument('--package', type=Path,
                        default=Path(__file__).resolve().parents[2] /
                        '.cache/deps/dill-0.4.1-wheel')
    parser.add_argument('--case', action='append', help='Upstream test filename; repeat to select')
    parser.add_argument('--timeout', type=float, default=60)
    args = parser.parse_args()
    package = args.package.resolve()
    suite = package / 'dill/tests'
    scripts = ([suite / name for name in args.case] if args.case else
               sorted(suite.glob('test_*.py')))
    if not scripts or any(not path.is_file() for path in scripts):
        parser.error('dill upstream test scripts not found in package directory')
    binary = args.binary.resolve()
    failures = 0
    for script in scripts:
        # Execute in the real __main__ namespace, preserving dill's global lookup.
        command = (f'import sys; sys.path[:0] = {[str(package), str(suite)]!r}; '
                   f'sys.argv = {[str(script)]!r}; __file__ = {str(script)!r}; '
                   "exec(compile(open(__file__).read(), __file__, 'exec'))")
        try:
            result = subprocess.run([str(binary), '-c', command], capture_output=True,
                                    text=True, timeout=args.timeout)
        except subprocess.TimeoutExpired:
            print(f'FAIL {script.name}: exceeded {args.timeout:g} seconds', flush=True)
            failures += 1
            continue
        if result.returncode:
            print(f'FAIL {script.name}: exit {result.returncode}', flush=True)
            print(result.stdout, end='')
            print(result.stderr, end='', flush=True)
            failures += 1
        else:
            print(f'PASS {script.name}', flush=True)
    print(f'{len(scripts) - failures}/{len(scripts)} upstream dill scripts passed')
    return bool(failures)


if __name__ == '__main__':
    raise SystemExit(main())
