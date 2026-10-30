"""Exercise the independent generated parser, not the runtime compiler subset."""

import argparse
import ast
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import subprocess
import unittest


VALID = [
    '', '# comment\n', 'x = ...\n',
    'a = 0x_FF + 0b_10 + 0o_77 + 1_000 + .5e+2 + 2.0j\n',
    "s = r'\\n' u'a'\nb = rb'\\xff' b'abc'\n",
    "s = f'{1 + 2!r:>{4}}' 'tail'\n",
    "s = f\"{ {'key': 3}[\"key\"] }\"\n",
    "s = f'''{1 # comment\n + 2=}'''\n",
    'a, *b, c = [1, 2, 3]\ndel a, b[::2]\nx += 3\n',
    'a = [*x, y]\nb = (*x, y)\nc = {**x, "y": z}\nd = {*x, y}\n',
    'a = [x for x in xs if x for y in ys]\nb = {x: y for x, y in xs}\n'
    'c = {x for x in xs}\nd = (x for x in xs)\nf(x for x in xs)\n',
    'a = x[1:2:3, ..., *xs]\nb = x if y else z\nc = (x := 3)\n',
    'assert x, y\nraise x from y\nglobal x, y\npass; x = 1\n',
    'if x:\n    pass\nelif y:\n    pass\nelse:\n    pass\n',
    'while x:\n    break\nelse:\n    pass\n'
    'for a, *b in xs:\n    continue\nelse:\n    pass\n',
    'try:\n    x\nexcept (X, Y) as e:\n    raise\nelse:\n    x\nfinally:\n    x\n',
    'try:\n    x\nexcept* X as e:\n    x\nexcept* Y:\n    x\n',
    'with (a as x, b as y,):\n    pass\n',
    'import a.b as c, d\nfrom ...pkg import (x as y, z,)\nfrom p import *\n',
    '@f(x)\n@x.y\ndef f(a: T, /, b=1, *args: T, c: T=2, **kw: T) -> R:\n'
    '    nonlocal x\n    yield from xs\n    return\n',
    'f = lambda a=1, /, b=2, *args, c=3, **kw: a + b\n'
    'f(*xs, k=1, **kw)\n',
    '@decorate\nclass C[T: Bound, *Ts, **P](A, *bases, metaclass=M, **kw):\n'
    '    x: int = 1\n    type Alias[X] = list[X]\n',
    'async def f[T](x):\n    async with x as y:\n        await y\n'
    '    async for a in x:\n        yield a\n'
    '    return [a async for a in x if a]\n',
    'match x:\n    case {"a": y, **rest} if y:\n        pass\n'
    '    case [1, *xs] | (2, *xs):\n        pass\n'
    '    case C(a, key=b) as value:\n        pass\n'
    '    case None | True | False:\n        pass\n'
    '    case -2 | 1+2j:\n        pass\n'
    '    case _:\n        pass\n',
    'match = 1\ncase = 2\ntype = 3\n',
    '变量 = 1\né = 2\na\u0301 = 3\nK = 4\nｉｆ = 5\n',
    'x = (\n    1\n) + 2\n'
    'def f(\n    value: str = "x",\n) -> str:\n    return value\n',
    "x = f'{42:=6}'\ny = f'{42!s:>{4}}'\n",
    "x = '\\N{LATIN SMALL LETTER A}\\N{HANGUL SYLLABLE GA}\\N{CJK UNIFIED IDEOGRAPH-4E00}\\N{NULL}'\n",
    'x = (\n  1 +\n  2\n)\ny = 1 + \\\n+ 2\n',
]

INVALID = [
    'x =', 'x = 09', 'x = 1__0', 'x = 0x', 'x = 1e+', 'x = 0b2',
    'if x\n    pass\n', 'if x:\npass\n', 'x = (]',
    'try: pass\n', 'try: pass\nelse: pass\n',
    'try: pass\nexcept X: pass\nexcept* Y: pass\n',
    'with : pass\n', 'def f(: pass', 'class C(: pass',
    'lambda a=1, b: b', 'f(x=1, 2)', 'f(**x, *y)',
    'x := 1', '(a.b := 1)', 'x = [x for in xs]',
    'x = {**}', 'x = {a:}', 'x = "unterminated',
    'x = f"{}"', 'x = f"{x!q}"', 'x = f"}"', 'x = f"{x"',
    'x = "\\x0"', 'x = "\\u123"',
    'x = "\\U00110000"', '😀 = 1', 'a\u200b = 2', '\u0301a = 2',
    'x = f"{x! r}"', 'x = f"{x!repr}"',
    "x = '\\N{NO SUCH CHARACTER}'", "x = '\\N{KEYCAP DIGIT ONE}'",
    "b'a' 'b'", "b'a' f'{x}'", "'a' b'b'",
    'match x:\n    case 1+2: pass\n', 'match x:\n    case 1j+2j: pass\n',
    '\ufeff#coding: utf8\nprint("text")\n',
    'match x:\n    case : pass\n', 'type A[] = int\n',
]


class GrammarDifferential(unittest.TestCase):
    observations = []

    def compare(self, source, mode='exec', expected=None):
        option = {'exec': [], 'eval': ['--eval'], 'single': ['--single'], 'func_type': ['--func-type']}[mode]
        actual = subprocess.run([self.binary, *option], input=source, text=True,
                                capture_output=True, timeout=15)
        reference = subprocess.run([self.reference, '-c',
                                    'import ast,sys; ast.parse(sys.stdin.read(), mode=sys.argv[1])', mode],
                                   input=source, text=True, capture_output=True, timeout=15)
        self.observations.append(dict(source=source, mode=mode,
                                      actual=actual.returncode, reference=reference.returncode,
                                      stderr=actual.stderr, reference_stderr=reference.stderr))
        if expected is not None:
            self.assertEqual(reference.returncode == 0, expected, reference.stderr)
        self.assertIn(actual.returncode, (0, 1), actual.stderr)
        self.assertEqual(actual.returncode == 0, reference.returncode == 0,
                         f'{source[:500]!r} ({len(source)} characters)\nactual: {actual.stderr}\nreference: {reference.stderr}')

    def test_complete_grammar_families(self):
        for source in VALID:
            with self.subTest(source=source):
                self.compare(source, expected=True)

    def test_rejected_syntax(self):
        for source in INVALID:
            with self.subTest(source=source):
                self.compare(source, expected=False)

    def test_entry_modes(self):
        for source, mode in [('x + 1', 'eval'), ('(x for x in xs)', 'eval'),
                             ('x = 1\n', 'single'), ('(int, str) -> bool', 'func_type')]:
            with self.subTest(source=source, mode=mode):
                self.compare(source, mode, True)

    def test_corpus(self):
        for path in self.corpus:
            try:
                source = path.read_text(encoding='utf-8')
            except UnicodeDecodeError:
                continue
            with self.subTest(path=str(path)):
                self.compare(source)
        for path, source, mode in self.syntax_cases:
            with self.subTest(upstream=str(path), source=source):
                self.compare(source, mode)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True)
    parser.add_argument('--reference', required=True)
    parser.add_argument('--corpus', type=Path, action='append', default=[])
    parser.add_argument('--syntax-tests', type=Path, action='append', default=[])
    parser.add_argument('--report', type=Path)
    args, remaining = parser.parse_known_args()
    GrammarDifferential.binary = args.binary
    GrammarDifferential.reference = args.reference
    GrammarDifferential.corpus = [path for root in args.corpus
                                 for path in (sorted(root.rglob('*.py')) if root.is_dir() else [root])]
    GrammarDifferential.syntax_cases = []
    for path in args.syntax_tests:
        for node in ast.walk(ast.parse(path.read_text(encoding='utf-8'))):
            if (isinstance(node, ast.Call) and isinstance(node.func, ast.Attribute)
                    and node.func.attr == '_check_error' and node.args
                    and isinstance(node.args[0], ast.Constant) and isinstance(node.args[0].value, str)):
                mode = next((kw.value.value for kw in node.keywords if kw.arg == 'mode'
                             and isinstance(kw.value, ast.Constant)), 'exec')
                GrammarDifferential.syntax_cases.append((path, node.args[0].value, mode))
    result = unittest.main(argv=[__file__, *remaining], exit=False).result
    if args.report:
        args.report.write_text(json.dumps(dict(
            captured_at=datetime.now(timezone.utc).isoformat(),
            passed=result.wasSuccessful(),
            binary_sha256=hashlib.sha256(Path(args.binary).read_bytes()).hexdigest(),
            reference_sha256=hashlib.sha256(Path(args.reference).read_bytes()).hexdigest(),
            reference_version=subprocess.check_output([args.reference, '--version'], text=True).strip(),
            grammar_sha256=hashlib.sha256(Path('Grammar/python.gram').read_bytes()).hexdigest(),
            observations=GrammarDifferential.observations), ensure_ascii=True, indent=2) + '\n')
    raise SystemExit(not result.wasSuccessful())
