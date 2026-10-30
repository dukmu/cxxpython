"""Run copied standard-library doctests through the actual product interpreter."""

import argparse
import ast
import doctest
from pathlib import Path
import subprocess

from test_cases import compare


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True, type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    binary = args.binary.resolve()
    checks = 0
    failures = 0
    # CPython Lib/test/test_heapq.py load_tests explicitly tests merge's docstring.
    module = ast.parse((root / 'Lib/heapq.py').read_text())
    merge = next(node for node in module.body
                 if isinstance(node, ast.FunctionDef) and node.name == 'merge')
    examples = doctest.DocTestParser().get_examples(ast.get_docstring(merge))
    source = 'from heapq import *\n'
    expected = b''
    for example in examples:
        source += f'exec(compile({example.source!r}, "<heapq.merge doctest>", "single"))\n'
        expected += example.want.encode()
        checks += 1
    result = subprocess.run([str(binary)], input=source.encode(), capture_output=True,
                            cwd=root, timeout=30)
    failures += not compare('heapq.merge doctests/stdout', expected, result.stdout)
    failures += not compare('heapq.merge doctests/stderr', b'', result.stderr)
    if result.returncode:
        print(f'heapq.merge doctests: exit {result.returncode}')
        failures += 1

    module = ast.parse((root / 'Lib/hashlib.py').read_text())
    doc = next(node.value.value for node in module.body
               if isinstance(node, ast.Assign)
               and any(isinstance(target, ast.Name) and target.id == '__doc__'
                       for target in node.targets))
    examples = doctest.DocTestParser().get_examples(doc)
    source = ''
    expected = b''
    for example in examples:
        source += f'exec(compile({example.source!r}, "<hashlib doctest>", "single"))\n'
        expected += example.want.encode()
        checks += 1
    result = subprocess.run([str(binary)], input=source.encode(), capture_output=True,
                            cwd=root, timeout=30)
    failures += not compare('hashlib doctests/stdout', expected, result.stdout)
    failures += not compare('hashlib doctests/stderr', b'', result.stderr)
    if result.returncode:
        print(f'hashlib doctests: exit {result.returncode}')
        failures += 1
    print(f'{checks} upstream stdlib doctest examples; {failures} failures')
    return bool(failures)


if __name__ == '__main__':
    raise SystemExit(main())
