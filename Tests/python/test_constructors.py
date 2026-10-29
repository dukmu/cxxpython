"""Host-side differential tests of numeric and constructor slices."""

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import random
import shutil
import subprocess
import unittest


class ConstructorDifferential(unittest.TestCase):
    observations = []

    def execute(self, command):
        result = subprocess.run(command, capture_output=True, text=True, timeout=15)
        self.observations.append(dict(command=command, stdout=result.stdout,
                                      stderr=result.stderr, exit_code=result.returncode))
        return result

    def check_expression(self, expression, setup=""):
        source = setup + "\ntry:\n    result = " + expression + (
            "\nexcept BaseException as error:\n"
            "    result = type(error).__name__\n"
        )
        actual = self.execute([self.binary, "-c", source, "--eval", "result"])
        reference = self.execute([self.reference, "-c", source + "\nprint(repr(result))"])
        self.assertEqual(reference.returncode, 0, reference.stderr)
        self.assertEqual(actual.returncode, 0, actual.stderr)
        self.assertEqual(actual.stdout, reference.stdout)

    def test_integer_strings_and_bases(self):
        cases = (
            "int()", "int(42)", "int(True)", "int('  -123  ')",
            "int('1_000')", "int('0xff', 0)", "int('-0B_101', base=0)",
            "int('0o77', 8)", "int('z', 36)", "int('000', 0)",
            "int('01', 0)", "int('0x', 0)", "int('1__2')", "int('_1')",
            "int('1_', 10)", "int('123', 1)", "int('123', 37)",
            "int('12', -1)", "int(12, 10)", "int(base=10)",
            "int('12', 10, base=10)", "int(x='12')", "int('١٢٣')",
            "int('\\u2003 42 \\u2003')", "int('123456789012345678901234567890')",
        )
        for expression in cases:
            with self.subTest(expression=expression):
                self.check_expression(expression)

    def test_integer_protocols_and_subclasses(self):
        setup = """
class ViaInt:
    def __int__(self):
        return 17
    def __index__(self):
        return 19
class ViaIndex:
    def __index__(self):
        return 23
class BadInt:
    def __int__(self):
        return 'bad'
class ViaTrunc:
    def __trunc__(self):
        return ViaIndex()
class BadTrunc:
    def __trunc__(self):
        return 'bad'
class Radix:
    def __index__(self):
        return 16
class Child(int):
    pass
class Text(str):
    def __int__(self):
        return 31
    def __index__(self):
        return 16
"""
        for expression in (
            "int(ViaInt())", "int(ViaIndex())", "int(BadInt())", "int(ViaTrunc())",
            "int(BadTrunc())",
            "int('ff', Radix())", "Child('123')", "type(Child(1)).__name__",
            "int(Text('12'))", "int(Text('12'), 10)",
            "type(int(Child(1))).__name__", "bool()", "bool(0)",
            "bool(Child(2))", "bool(1, 2)", "bool(x=1)",
        ):
            with self.subTest(expression=expression):
                self.check_expression(expression, setup)

    def test_float_construction(self):
        setup = """
class ViaFloat:
    def __float__(self):
        return float('2.5')
class ViaIndex:
    def __index__(self):
        return 7
class BadFloat:
    def __float__(self):
        return 1
class Child(float):
    pass
class Text(str):
    def __float__(self):
        return float('3.5')
class Slotted(float):
    __slots__ = ('item',)
slotted = Slotted('1.5')
slotted.item = (7, 8)
"""
        for expression in (
            "float()", "float(42)", "float('  -1.25e2  ')",
            "float('inf')", "float('-Infinity')", "float('nan')", "float('nan(foo)')",
            "float('+-1')", "float('+-inf')", "float('++1')", "float('--1')",
            "float('+1')", "float('+Infinity')", "float('+nan')", "float('-0')",
            "float('1_2.5')", "float('1__2')", "float(ViaFloat())",
            "float(Text('2.5'))", "float('١٢.٥')", "float('1e16')",
            "float('1e-5')", "int(float('-123.75'))",
            "float('1e9999')", "float('-1e9999')", "float('1e-9999')",
            "float('-1e-9999')", "float('5e-324')", "float('1.7976931348623157e308')",
            "float('9007199254740992') == 9007199254740993",
            "float('9007199254740992') == 9007199254740992",
            "float(9007199254740993)", "float(9007199254740995)",
            "float(9686040848368617267738905405359029371380096248252945222673)",
            "float(ViaIndex())", "float(BadFloat())", "float(x=1)",
            "type(Child(1)).__name__", "hash(float(42)) == hash(42)",
            "float(42) == 42", "float('nan') == float('nan')",
            "slotted, slotted.item, type(slotted).__name__",
        ):
            with self.subTest(expression=expression):
                self.check_expression(expression, setup)

    def test_tuple_construction(self):
        setup = """
original = (1, 2)
class Child(tuple):
    pass
class Changed(tuple):
    def __iter__(self):
        return iter((9, 8))
child = Child(original)
changed = Changed(original)
events = 0
class Count:
    def __index__(self):
        global events
        events = events * 10 + 3
        return 2
class Source:
    def __iter__(self):
        global events
        events = events * 10 + 1
        return iter((4, 5))
    def __len__(self):
        global events
        events = events * 10 + 2
        return Count()
value = tuple(Source())
class Hint:
    def __iter__(self):
        return iter((1, 2))
    def __len__(self):
        raise TypeError('no length')
    def __length_hint__(self):
        return NotImplemented
class Negative(Hint):
    def __length_hint__(self): return -1
class Invalid(Hint):
    def __length_hint__(self): return Count()
class Huge(Hint):
    def __length_hint__(self): return 18446744073709551616
class Declined(Hint):
    def __length_hint__(self): raise TypeError('no hint')
class Failed(Hint):
    def __length_hint__(self): raise ValueError('bad hint')
class Disabled(Hint):
    __length_hint__ = None
class Descriptor:
    def __get__(self, obj, owner): raise TypeError('descriptor failed')
class BadDescriptor(Hint):
    __length_hint__ = Descriptor()
class After:
    def __length_hint__(self): raise ValueError('new type')
class Retyped:
    def __iter__(self): return iter((1, 2))
    def __len__(self):
        self.__class__ = After
        raise TypeError('no length')
class Broken:
    def __iter__(self): return self
    def __next__(self): raise ValueError('not exhaustion')
"""
        for expression in (
            "tuple()", "tuple((1, 2))", "tuple(range(3))", "tuple(1)",
            "tuple(iterable=(1, 2))", "tuple(None)", "tuple(1, 2)",
            "tuple(original) is original", "tuple(child), type(child).__name__",
            "tuple(changed)", "tuple('a\\U0001f600')", "value, events",
            "tuple(Hint())", "tuple(Negative())", "tuple(Invalid())",
            "tuple(Huge())", "tuple(Declined())", "tuple(Failed())",
            "tuple(Disabled())", "tuple(BadDescriptor())", "tuple(Retyped())",
            "tuple(Broken())",
        ):
            with self.subTest(expression=expression):
                self.check_expression(expression, setup)

    def test_float_operators_and_source_literals(self):
        huge = "int(" + repr("9" * 400) + ")"
        for expression in (
            "0., .5, 1.e2, 001.25, 1_2.3_4e+0_2, 1e309, -1e-9999",
            "1.25 + 2, 2 + 1.25, 3 - .5, .5 - 3, 2 * .25, .25 * 2",
            "7.5 / 2, 7 / 2.5, 7.5 // 2, 7 // 2.5, -7.5 % 2, 7.5 % -2",
            "-0.0 / 3, 0.0 / -3, -0.0 // 3, 0.0 // -3, 0.0 % -3",
            "1.0 // .1, 1.0 % .1, -1.0 // .1, -1.0 % .1",
            "float('nan') < 1, float('nan') >= 1, float('nan') != 1",
            "float('inf') > " + huge + ", float('-inf') < -" + huge,
            "9007199254740992.0 < 9007199254740993, 9007199254740993 > 9007199254740992.0",
            "1.5 > 1, 1.5 < 2, -1.5 < -1, -1.5 > -2, -.5 < 0",
            "float('inf') // 2, 2.0 // float('-inf'), -2.0 % float('inf')",
            "+1.5, -(-0.0), +(-0.0), not 0.0, bool(float('nan'))",
            "1.0 / 0, 1.0 // 0.0, float('nan') % 0.0",
            "1.0 + " + huge, "1 / 0", "1.0 + '2'", "~1.0",
            "float.__rsub__(2.5, 9)", "float.__add__(1, 1.0)",
        ):
            with self.subTest(expression=expression):
                self.check_expression(expression)
        setup = """
events = 0
class Left:
    def __itruediv__(self, other):
        global events
        events = events * 10 + 1
        return NotImplemented
    def __truediv__(self, other):
        global events
        events = events * 10 + 2
        return NotImplemented
class Right:
    def __rtruediv__(self, other):
        global events
        events = events * 10 + 3
        return 42
class Child(float):
    def __radd__(self, other): return 97
class Pretend:
    def __float__(self): return 9.0
value = Left()
value /= Right()
number = 8
number /= 4
"""
        for expression in ("value, events, number, 1.0 + Child(2.0)", "1.0 + Pretend()"):
            with self.subTest(expression=expression):
                self.check_expression(expression, setup)

    def test_integer_true_division(self):
        pairs = [
            (7, 2), (0, -1), (1, 0),
            ((1 << 4000) + 1, 1 << 4000),
            ((1 << 53) + 1, 1 << 53), ((1 << 53) + 3, 1 << 53),
            (1, 1 << 1074), (1, 1 << 1075), (-1, 1 << 1075), (3, 1 << 1075),
            ((1 << 1024) - (1 << 971), 1),
            ((1 << 1024) - (1 << 970), 1),
            ((1 << 1024) - (1 << 970) - 1, 1),
        ]
        rng = random.Random(31214)
        for _ in range(40):
            a = rng.getrandbits(rng.randrange(1, 2000))
            b = rng.getrandbits(rng.randrange(1, 2000)) or 1
            pairs.append((a if rng.randrange(2) else -a, b if rng.randrange(2) else -b))
        for a, b in pairs:
            expression = f"({a}) / ({b})"
            with self.subTest(expression=expression):
                self.check_expression(expression)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    parser.add_argument("--reference", required=True)
    parser.add_argument("--report", type=Path)
    options, remaining = parser.parse_known_args()
    ConstructorDifferential.binary = options.binary
    ConstructorDifferential.reference = options.reference
    version = subprocess.check_output([options.reference, "--version"], text=True).strip()
    print("Reference:", version, flush=True)
    suite = unittest.main(argv=[__file__, *remaining], exit=False)
    if options.report:
        provenance = {}
        for label, command in [("binary", options.binary), ("reference", options.reference)]:
            path = Path(shutil.which(command) or command).resolve()
            provenance[label] = dict(path=str(path), sha256=hashlib.sha256(path.read_bytes()).hexdigest())
        options.report.write_text(json.dumps(dict(captured_at=datetime.now(timezone.utc).isoformat(),
            reference_version=version, provenance=provenance, passed=suite.result.wasSuccessful(),
            observations=ConstructorDifferential.observations), ensure_ascii=True, indent=2) + "\n")
    raise SystemExit(not suite.result.wasSuccessful())
