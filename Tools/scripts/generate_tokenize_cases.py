#!/usr/bin/env python3
"""Extract literal tokenizer samples from the pinned CPython test suite."""

import argparse
import ast
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('upstream', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--batch-size', type=int, default=48)
    args = parser.parse_args()
    if args.batch_size < 1:
        parser.error('--batch-size must be positive')
    source = args.upstream.read_bytes()
    tree = ast.parse(source)
    samples = []
    for node in ast.walk(tree):
        if (isinstance(node, ast.Call) and isinstance(node.func, ast.Attribute)
                and node.func.attr == 'check_tokenize' and node.args
                and isinstance(node.args[0], ast.Constant)
                and isinstance(node.args[0].value, str)):
            samples.append((node.lineno, node.args[0].value))
    samples.sort()
    header = ('# Generated from CPython 3.12.14 Lib/test/test_tokenize.py.\n'
              '# PSF license: doc/licenses/python-3.12.14.txt.\n'
              '# Regenerate with Tools/scripts/generate_tokenize_cases.py.\n'
              'import _tokenize\nimport io\nimport tokenize\n\nSAMPLES = (\n')
    runner = '''
)

for line, source in SAMPLES:
    for mode in ('text', 'bytes', 'native'):
        if mode == 'text':
            iterator = tokenize.generate_tokens(io.StringIO(source).readline)
        elif mode == 'bytes':
            iterator = tokenize.tokenize(io.BytesIO(source.encode()).readline)
        else:
            iterator = _tokenize.TokenizerIter(io.StringIO(source).readline, extra_tokens=False)
        values = []
        try:
            for item in iterator:
                values.append(tuple(item))
            error = None
        except Exception as exc:
            error = (type(exc).__name__, exc.args)
        print(line, mode, repr((values, error)))
'''
    for index, start in enumerate(range(0, len(samples), args.batch_size)):
        output = args.output
        if index:
            output = output.parent.with_name(f'{output.parent.name}_{index + 1}') / output.name
        batch = samples[start:start + args.batch_size]
        body = ''.join(f'    {sample!a},\n' for sample in batch)
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(header + body + runner, encoding='ascii')
        print(f'{len(batch)} upstream samples -> {output}')


if __name__ == '__main__':
    main()
