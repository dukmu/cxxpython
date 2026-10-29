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
        expected = self.execute([self.reference, "-c", source + "\nprint(repr(" + expression + "))"])
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
        ]
        for source, expression in cases:
            with self.subTest(expression=expression):
                self.check_program(source, expression)

    def test_compile_and_runtime_errors_stay_distinct(self):
        for source in ["return 1", "break", "if 1:\nx = 2", "def f(a, a): pass", "x = 1\nglobal x"]:
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
