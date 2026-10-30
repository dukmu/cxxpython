"""Differential grammar and evaluation tests against a CPython executable.

Each program is executed by both binaries; no host Python eval supplies runtime
semantics to the project interpreter.
"""

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
from textwrap import dedent
import unittest


class SyntaxDifferential(unittest.TestCase):
    observations = []

    def execute(self, command):
        result = subprocess.run(command, capture_output=True, text=True, timeout=15)
        self.observations.append(dict(command=command, stdout=result.stdout,
                                      stderr=result.stderr, exit_code=result.returncode))
        return result

    def check_program(self, source, expression):
        source = dedent(source).strip()
        actual = self.execute([self.binary, "-c", source, "--eval", expression])
        expected = self.execute([self.reference, "-c",
                                 source + "\nprint(repr((" + expression + ")))"])
        self.assertEqual(expected.returncode, 0, expected.stderr)
        self.assertEqual(actual.returncode, 0, actual.stderr)
        self.assertEqual(actual.stdout, expected.stdout)

    def check_cases(self, cases):
        for source, expression in cases:
            with self.subTest(source=source, expression=expression):
                self.check_program(source, expression)

    def test_literals_and_dictionary_unpacking(self):
        self.check_cases([
            ("", "..., ... is Ellipsis, {1: 2, 3: 4}, {}"),
            ("", "{**{1: 2}, 1: 3, **{4: 5}, 6: 7}"),
            ("""
             events = []
             def mark(tag, value):
                 events.append(tag)
                 return value
             result = {mark('key', 1): mark('value', 2),
                       **mark('unpack', {1: 3, 4: 5}),
                       mark('last-key', 4): mark('last-value', 6)}
             """, "result, events"),
            ("""
             class Mapping:
                 def keys(self): return ('a', 'b')
                 def __getitem__(self, key): return key + '!'
             result = {**Mapping()}
             """, "result"),
        ])

    def test_unicode_names_and_slices(self):
        self.check_cases([
            ('K = 7\na\u0301 = 9\nｉｆ = 3', 'K, á, getattr(__import__("__main__"), "if")'),
            ('a = list(range(8))\na[1:6:2] = (9, 8, 7)\ndel a[::-3]', 'a, (1, 2, 3)[::-1], "abcd"[1::2]'),
            ('a = [1, 2, 3]\na[1:1] = a', 'a, range(2, 20, 3)[::-2]'),
            ('', 'slice(1, 2, 3), hash(slice(1, 2, 3)), slice(None, None, -2).indices(9)'),
        ])

    def test_fstrings_use_format_protocols(self):
        self.check_cases([
            ('', 'f"{None} {True} {42} {1.25} {\'text\'} {[]} {()} {{}}"'),
            ('value = "你好"', 'f"{value!s}|{value!r}|{value!a}|{value=}"'),
            ('value = 31\nwidth = 8', 'f"{value:#08x}|{value:>{width}}|{3.14159:.2f}"'),
            ('', 'f"{ {\'key\': 3}[\'key\'] }" "tail"'),
            ('', 'f"{1 # comment\n + 2=}"'),
            ('''events = []
class Value:
    def __format__(self, spec):
        events.append(spec)
        return 'formatted'
value = Value()
text = f'{value:{3}}'
''', 'text, events'),
            ('''events = []
class Value:
    def __repr__(self):
        events.append('repr')
        return 'value'
def width():
    events.append('width')
    return 8
text = f'{Value()!r:>{width()}}'
''', 'text, events'),
            ('def f(): pass', 'f"{type(f.__code__).__name__}", f"{f.__code__.co_name}"'),
        ])

    def test_operator_precedence_and_numeric_edges(self):
        self.check_cases([
            ("", "-2 ** 2, (-2) ** 2, 2 ** 3 ** 2, 2 ** -2"),
            ("", "1 + 2 << 3, 64 >> 1 + 2, 1 | 2 ^ 3 & 6, ~1 ** 2"),
            ("", "-5 >> 1, -5 & 3, -5 ^ 1, -5 | 1, 1 << 100"),
            ("", "True & False, True | False, True ^ True, type(True & True) is bool"),
            ("", "type(int.__and__(True, True)) is int, type(bool.__and__(True, True)) is bool, True & 1, 1 & True"),
            ("", "2 ** 100, 0 ** 0, (-1) ** 101, 4.0 ** -1, (-2.0) ** 3"),
            ("", "1 in (1, 2), 3 not in (1, 2), 1 not in (1,), 0 < 1 in (1, 2)"),
            ("", "'bc' in 'abcd', '' in 'abc', 'ac' not in 'abc', 'key' in {'key': 1}"),
            ("""
             events = []
             def value(name, result):
                 events.append(name)
                 return result
             result = value('a', 2) ** value('b', 3) ** value('c', 2)
             """, "result, events"),
            ("""
             events = []
             class Left:
                 def __matmul__(self, other):
                     events.append('left')
                     return NotImplemented
             class Right:
                 def __rmatmul__(self, other):
                     events.append('right')
                     return 23
             result = Left() @ Right()
             """, "result, events"),
            ("""
             events = []
             class Base:
                 def __pow__(self, other):
                     events.append('base')
                     return 1
             class Child(Base):
                 def __rpow__(self, other):
                     events.append('reflected')
                     return 2
             result = Base() ** Child()
             """, "result, events"),
            ("""
             events = []
             class Box:
                 def __imatmul__(self, other):
                     events.append('inplace')
                     return 17
             x = Box()
             x @= 3
             """, "x, events"),
        ])

    def test_membership_protocol_and_iteration(self):
        self.check_cases([
            ("""
             events = []
             class Container:
                 def __contains__(self, item):
                     events.append(item)
                     return [1]
             x = 4 in Container()
             y = 5 not in Container()
             """, "x, y, events"),
            ("""
             events = []
             class Item:
                 def __eq__(self, other):
                     events.append('eq')
                     return False
             class Container:
                 def __iter__(self):
                     events.append('iter')
                     return iter((item,))
             item = Item()
             result = item in Container()
             """, "result, events"),
            ("""
             events = []
             class Item:
                 def __eq__(self, other):
                     events.append('eq')
                     return other == 9
             result = 9 in (Item(),)
             """, "result, events"),
        ])

    def test_conditional_lambda_and_walrus(self):
        self.check_cases([
            ("", "1 if True else 2 if False else 3, 0 if False else 2 if True else 3"),
            ("""
             events = []
             def mark(value):
                 events.append(value)
                 return value
             result = mark(1) if mark(0) else mark(2)
             """, "result, events"),
            ("""
             events = []
             def mark(value):
                 events.append(value)
                 return value
             f = lambda a=mark(1), /, b=mark(2), *args, c=mark(3), **kw: (a, b, args, c, kw)
             before = list(events)
             result = f(7, 8, 9, c=10, extra=11)
             """, "before, events, result, f()"),
            ("""
             def outer(x):
                 return lambda y: x + y
             f = outer(10)
             """, "f(5), f(7)"),
            ("""
             x = 0
             result = (x := 3) + (x := x + 4)
             if (y := x * 2):
                 z = y
             f = lambda: (local := 5)
             """, "result, x, y, z, f()"),
            ("""
             events = []
             def mark(value):
                 events.append(value)
                 return value
             result = False and (x := mark(1))
             other = True or (y := mark(2))
             """, "result, other, events"),
        ])

    def test_assert_evaluation_and_errors(self):
        self.check_cases([
            ("""
             events = []
             def message():
                 events.append('message')
                 return 42
             assert True, message()
             try:
                 assert False, message()
             except AssertionError as error:
                 args = error.args
             """, "events, args"),
            ("""
             try:
                 assert 0
             except AssertionError as error:
                 args = error.args
             """, "args"),
        ])

    def test_starred_displays_targets_and_calls(self):
        self.check_cases([
            ("", "[*range(3), 9, *(10, 11)], (0, *range(3), 4), (*(),)"),
            ("""
             events = []
             def mark(name, value):
                 events.append(name)
                 return value
             result = [mark('a', 1), *mark('b', (2, 3)), mark('c', 4)]
             """, "result, events"),
            ("""
             a, *middle, z = range(5)
             [first, *rest] = (8,)
             (head, *(inner, tail)) = (1, 2, 3)
             """, "a, middle, z, first, rest, head, inner, tail"),
            ("""
             result = []
             for a, *b in ((1, 2, 3), (4,)):
                 result.append((a, b))
             """, "result"),
            ("""
             events = []
             def mark(name, value):
                 events.append(name)
                 return value
             def f(a, b, *args, c=0, **kw): return (a, b, args, c, kw)
             result = f(mark('a', 1), *mark('star', (2, 3)),
                        c=mark('c', 4), **mark('kwargs', {'d': 5}))
             """, "result, events"),
            ("""
             def f(*args, **kw): return args, kw
             result = f(*[1], *(2, 3), **{'x': 4}, **{'y': 5})
             """, "result"),
            ("""
             def f(*args, **kw): return args, kw
             events = []
             def mark(name, value):
                 events.append(name)
                 return value
             result = f(k=mark('keyword', 1), *mark('star', (2,)))
             """, "result, events"),
        ])

    def test_comprehensions(self):
        self.check_cases([
            ("", "[x * x for x in range(5) if x % 2], {x: x * x for x in range(3)}"),
            ("", "[(x, y) for x in range(3) if x for y in range(x) if y == 0]"),
            ("", "{x % 2: x for x in range(4)}"),
            ("""
             events = []
             def mark(name, value):
                 events.append(name)
                 return value
             result = {mark('key', x): mark('value', x + 1) for x in mark('iterable', (1, 2))}
             """, "result, events"),
            ("""
             x = 99
             values = [x for x in (1, 2, 3)]
             """, "x, values"),
            ("""
             def outer():
                 x = 8
                 values = [x + y for y in (1, 2)]
                 return x, values
             """, "outer()"),
            ("""
             x = 0
             values = [(x := i) for i in (1, 2, 3)]
             """, "x, values"),
            ("""
             x = (1, 2)
             values = [x for x in x]
             """, "x, values"),
            ("""
             def outer():
                 x = 0
                 values = [(x := i) for i in range(4)]
                 return x, values
             """, "outer()"),
            ("""
             def outer():
                 x = 0
                 def inner():
                     nonlocal x
                     return [(x := i) for i in range(4)]
                 result = inner()
                 return x, result
             """, "outer()"),
            ("""
             x = 41
             class C:
                 x = 99
                 values = [x for _ in (1,)]
             """, "C.values, C.x, x"),
            ("", "[f() for f in [lambda: x for x in range(3)]]"),
            ("", "[[x + y for y in range(2)] for x in range(3)]"),
        ])

    def test_runtime_errors(self):
        cases = [
            ("0 ** -1", "ZeroDivisionError"),
            ("1 << -1", "ValueError"),
            ("1 >> -1", "ValueError"),
            ("{**None}", "TypeError"),
            ("[*None]", "TypeError"),
            ("a, *b, c = (1,)", "ValueError"),
            ("def f(**kw): pass\nf(**{'x': 1}, **{'x': 2})", "TypeError"),
            ("def f(**kw): pass\nf(**{1: 2})", "TypeError"),
            ("def f(*args): pass\nf(*None)", "TypeError"),
            ("1 in None", "TypeError"),
            ("1 in 'abc'", "TypeError"),
            ("[] in {}", "TypeError"),
        ]
        for source, error in cases:
            with self.subTest(source=source):
                expected = self.execute([self.reference, "-c", source])
                actual = self.execute([self.binary, "-c", source])
                self.assertNotEqual(expected.returncode, 0, expected.stdout)
                self.assertIn(error, expected.stderr)
                self.assertEqual(actual.returncode, 1, actual.stderr)
                self.assertIn(error, actual.stderr)

    def test_invalid_syntax(self):
        cases = [
            "x := 1", "x = y := 1", "assert x := 1", "return_value = lambda: x := 1",
            "f(x := 1, y=2) = 0", "(a.b := 1)", "(a[0] := 1)",
            "a, *b, *c = (1, 2, 3)", "[*a, *b] = (1, 2)",
            "*a = (1, 2)", "del *a", "f(**{'x': 1}, 2)",
            "f(x=1, x=2)", "f(**{'x': 1}, *())", "{**}",
            "lambda a, /, a: a", "lambda *, x, x: x", "lambda a=1, b: b",
            "[x for x in (1, 2) if]", "{x: for x in (1, 2)}",
            "[x for x in (y := (1, 2))]", "[(x := 1) for x in (1, 2)]",
            "class C:\n    [(x := i) for i in (1, 2)]",
            "[i for i in range(3) if (j := i) for j in range(2)]",
            "[j for i in range(3) for j in (x := range(2))]",
            "(]", "[}", "{)",
        ]
        for source in cases:
            with self.subTest(source=source):
                expected = self.execute([self.reference, "-c", source])
                actual = self.execute([self.binary, "-c", source])
                self.assertNotEqual(expected.returncode, 0, expected.stdout)
                self.assertIn("SyntaxError", expected.stderr)
                self.assertEqual(actual.returncode, 1, actual.stderr)
                self.assertIn("SyntaxError", actual.stderr)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True)
    parser.add_argument("--reference", required=True)
    parser.add_argument("--report", type=Path)
    args, remaining = parser.parse_known_args()
    SyntaxDifferential.binary, SyntaxDifferential.reference = args.binary, args.reference
    version = subprocess.check_output([args.reference, "--version"], text=True).strip()
    print("Reference:", version, flush=True)
    suite = unittest.main(argv=[__file__, *remaining], exit=False)
    if args.report:
        provenance = {}
        for label, command in [("binary", args.binary), ("reference", args.reference)]:
            path = Path(shutil.which(command) or command).resolve()
            provenance[label] = dict(path=str(path), sha256=hashlib.sha256(path.read_bytes()).hexdigest())
        args.report.write_text(json.dumps(dict(captured_at=datetime.now(timezone.utc).isoformat(),
                                               reference_version=version, provenance=provenance,
                                               passed=suite.result.wasSuccessful(),
                                               observations=SyntaxDifferential.observations),
                                          ensure_ascii=True, indent=2) + "\n")
    raise SystemExit(not suite.result.wasSuccessful())
