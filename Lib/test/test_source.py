"""Run supported source workflows through both actual executables."""

import argparse
from datetime import datetime, timezone
import hashlib
import json
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


class SourceDifferential(unittest.TestCase):
    observations = []

    def execute(self, command):
        result = subprocess.run(command, capture_output=True, text=True, timeout=15)
        self.observations.append(dict(command=command, stdout=result.stdout, stderr=result.stderr, exit_code=result.returncode))
        return result

    def check_program(self, source, expression):
        actual = self.execute([self.binary, "-c", source, "--eval", expression])
        expected = self.execute([self.reference, "-c", source + "\nprint(repr((" + expression + ")))"])
        self.assertEqual(expected.returncode, 0, expected.stderr)
        self.assertEqual(actual.returncode, 0, actual.stderr)
        self.assertEqual(actual.stdout, expected.stdout)

    def test_source_workflows(self):
        cases = [
            ("", "0x_FF + 0b101 + 0o7 + 1_000"),
            ("", "-(18446744073709551616) // 3"),
            ("", "not 0 and 1 < 2 < 3 or missing"),
            ("", "'\\u4f60\\u597d' ' ' r'\\n'"),
            ("def f(n):\n    total = 0\n    while n:\n        total += n\n        n -= 1\n    return total", "f(2000)"),
            ("def fact(n):\n    if n <= 1:\n        return 1\n    return n * fact(n - 1)", "fact(20)"),
            ("count = 0\ndef bump():\n    global count\n    count += 1\n    return count\ndef f(a=bump(), *, b=bump()):\n    return a * 10 + b\nx = f()\ny = f()", "count * 100 + x + y"),
            ("def f(a, /, b=2, *, c=3):\n    return a + b + c", "f(1, c=4)"),
            ("def f(*args, **kwargs):\n    return len(args) * 10 + len(kwargs)", "f(1, 2, x=3)"),
            ("i = 0\nx = 0\nwhile i < 10:\n    i += 1\n    if i == 3:\n        continue\n    elif i == 7:\n        break\n    x += i\nelse:\n    x = 999", "x"),
            ("def factory():\n    def child():\n        return 42\n    return child\na = factory()\nb = factory()", "a is not b and a.__code__ is b.__code__"),
            ("'module doc'\ndef f():\n    'function doc'\n    return __debug__", "__doc__ + ' ' + f.__doc__"),
            ("'module doc'\nglobal __doc__", "__doc__"),
            ("def outer(x):\n    def middle():\n        def inner():\n            return x\n        return inner\n    return middle", "outer(42)()()"),
            ("def outer(x):\n    def read():\n        return x\n    def bump():\n        nonlocal x\n        x += 1\n    bump()\n    return read\na = outer(40)\nb = outer(10)", "a() * 100 + b()"),
            ("def outer(x, /, *args, flag=2, **kw):\n    def inner(step=x, *, extra=flag):\n        return x + flag + len(args) + len(kw) + step + extra\n    x += 1\n    return inner\na = outer(10, 1, 2, flag=3, key=1)", "a()"),
            ("def outer(z, a):\n    x = 1\n    y = 2\n    def inner():\n        return z + a + x + y\n    return inner", "outer.__code__.co_cellvars"),
            ("def outer(z, a):\n    x = 1\n    y = 2\n    def inner():\n        return z + a + x + y\n    return inner", "outer.__code__.co_varnames"),
            ("def outer(z, a):\n    x = 1\n    y = 2\n    def inner():\n        return z + a + x + y\n    return inner", "outer(1, 2).__code__.co_freevars"),
            ("def outer():\n    def fact(n):\n        if n <= 1:\n            return 1\n        return n * fact(n - 1)\n    return fact", "outer()(15)"),
            ("x = 42\ndef outer():\n    x = 1\n    def middle():\n        global x\n        def inner():\n            return x\n        return inner\n    return middle", "outer()()()"),
            ("def outer(x):\n    def read():\n        return x\n    return read\na = outer(1)\nb = outer(2)", "a is not b and a.__closure__ is not b.__closure__ and a.__code__ is b.__code__"),
        ]
        for source, expression in cases:
            with self.subTest(expression=expression):
                self.check_program(source, expression)

    def test_compile_and_runtime_errors_stay_distinct(self):
        for source in ["return 1", "break", "if 1:\nx = 2", "def f(a, a): pass", "x = 1\nglobal x",
                       "def f():\n    nonlocal missing", "def f(x):\n    nonlocal x", "def f():\n    global x\n    nonlocal x"]:
            with self.subTest(source=source):
                actual = self.execute([self.binary, "-c", source])
                expected = self.execute([self.reference, "-c", source])
                self.assertNotEqual(expected.returncode, 0)
                self.assertEqual(actual.returncode, 1)
                self.assertIn("Error:", actual.stderr)
        actual = self.execute([self.binary, "-c", "def f():\n    return 1 // 0\nx = f()"])
        self.assertEqual(actual.returncode, 1)
        self.assertIn("Traceback", actual.stderr)
        self.assertIn("line 2", actual.stderr)
        self.assertIn("ZeroDivisionError", actual.stderr)
        for source, error in [
            ("def f():\n    def inner():\n        return x\n    return x\n    x = 1\nf()", "UnboundLocalError"),
            ("def f():\n    x = 1\n    def inner():\n        return x\n    del x\n    return inner\nf()()", "NameError"),
        ]:
            actual = self.execute([self.binary, "-c", source])
            expected = self.execute([self.reference, "-c", source])
            self.assertEqual(actual.returncode, 1, actual.stderr)
            self.assertNotEqual(expected.returncode, 0)
            self.assertIn(error, actual.stderr)
            self.assertIn(error, expected.stderr)

    def test_iteration_unpacking_and_exception_workflows(self):
        cases = [
            ("", "(), (1,), (1, 2), ((1, 2), 3)"),
            ("a, (b, c) = (1, (2, 3))", "a, b, c"),
            ("a, b = range(2)", "a, b"),
            ("a, b = 'a\\U0001f600'", "a, b"),
            ("", "(10, 20, 30)[-2], 'a\\U0001f600'[-1]"),
            ("", "range(9, -2, -3), range(9, -2, -3)[-1]"),
            ("", "range(1000000000000000000000000000000, 1000000000000000000000000000020, 3)[-1]"),
            ("", "range(0, 3, 2) == range(0, 4, 2), range(0) == range(2, 2)"),
            ("", "hash(range(0, 3, 2)) == hash(range(0, 4, 2))"),
            ("", "bool(range(1000000000000000000000000000000)), bool(range(5, 5))"),
            ("it = iter((1, 2))\na = next(it)\nb = next(it)", "a, b, next(it, None), next(it, 7)"),
            ("it = iter(range(3))", "iter(it) is it"),
            ("total = 0\nfor a, b in ((1, 2), (3, 4)):\n    total += a * b", "total"),
            ("total = 0\nfor i in range(8):\n    if i == 2:\n        continue\n"
             "    if i == 6:\n        break\n    total += i\nelse:\n    total = 999", "total"),
            ("total = 0\nfor i in range(0):\n    total = 1\nelse:\n    total = 42", "total"),
            ("def outer():\n    for x in range(3):\n        def read():\n            return x\n"
             "    return read\nf = outer()", "f(), f.__code__.co_freevars"),
            ("def total(n):\n    for i in range(n):\n        return i\n    return None", "total(0), total(4)"),
            ("error = StopIteration(1, 2)\nerror.args = (3, 4)", "error.args, error.value"),
            ("error = StopIteration(1)\ndel error.value\nerror.args = range(3)", "error.args, error.value"),
            ("error = StopIteration(1)\nBaseException.__init__(error, 2)", "error.args, error.value"),
            ("", "ValueError('message').args, StopIteration.__new__(StopIteration, 9).value"),
        ]
        for source, expression in cases:
            with self.subTest(source=source, expression=expression):
                self.check_program(source, expression)

    def test_iteration_errors_preserve_python_exception_types(self):
        for source, error in [
            ("next(iter(()))", "StopIteration"),
            ("next(iter(range(0)))", "StopIteration"),
            ("iter(None)", "TypeError"),
            ("next((1, 2))", "TypeError"),
            ("range(0, 3, 0)", "ValueError"),
            ("range(stop=3)", "TypeError"),
            ("len(range(1000000000000000000000000000000))", "OverflowError"),
            ("range(3)[3]", "IndexError"),
            ("(1, 2)[1000000000000000000000000000000]", "IndexError"),
            ("'abc'[-4]", "IndexError"),
            ("(1, 2)[None]", "TypeError"),
            ("a, b = (1,)", "ValueError"),
            ("a, b = range(3)", "ValueError"),
            ("a, b = None", "TypeError"),
            ("value = (1, 2)\nvalue[0] = 3", "TypeError"),
            ("value = (1, 2)\ndel value[0]", "TypeError"),
            ("error = ValueError()\ndel error.args", "TypeError"),
        ]:
            with self.subTest(source=source):
                actual = self.execute([self.binary, "-c", source])
                expected = self.execute([self.reference, "-c", source])
                self.assertEqual(actual.returncode, 1, actual.stderr)
                self.assertNotEqual(expected.returncode, 0)
                self.assertIn(error, actual.stderr)
                self.assertIn(error, expected.stderr)

    def test_file_entry_and_unicode_diagnostics(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "source.py"
            path.write_text("def f(n):\n    return n + 1\n", encoding="utf-8")
            actual = self.execute([self.binary, str(path), "--eval", "f(41)"])
            self.assertEqual(actual.returncode, 0, actual.stderr)
            self.assertEqual(actual.stdout, "42\n")
        actual = self.execute([self.binary, "--eval", "'\\u4f60' + )"])
        self.assertEqual(actual.returncode, 1)
        self.assertIn("SyntaxError", actual.stderr)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True)
    parser.add_argument("--reference", required=True)
    parser.add_argument("--report", type=Path)
    args, remaining = parser.parse_known_args()
    SourceDifferential.binary, SourceDifferential.reference = args.binary, args.reference
    version = subprocess.check_output([args.reference, "--version"], text=True).strip()
    print("Reference:", version, flush=True)
    suite = unittest.main(argv=[__file__, *remaining], exit=False)
    if args.report:
        provenance = {}
        for label, command in [("binary", args.binary), ("reference", args.reference)]:
            path = Path(shutil.which(command) or command).resolve()
            provenance[label] = dict(path=str(path), sha256=hashlib.sha256(path.read_bytes()).hexdigest())
        args.report.write_text(json.dumps(dict(captured_at=datetime.now(timezone.utc).isoformat(), reference_version=version,
            provenance=provenance, passed=suite.result.wasSuccessful(), observations=SourceDifferential.observations), ensure_ascii=True, indent=2) + "\n")
    raise SystemExit(not suite.result.wasSuccessful())
