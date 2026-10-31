"""Compare the unittest module entry point with a reference interpreter."""

import argparse
from pathlib import Path
import re
import subprocess
import tempfile
import unittest


def normalize_timing(data):
    return re.sub(rb'(?m)^(Ran \d+ tests? in )\d+\.\d+s$',
                  rb'\1<elapsed>s', data)


class UnittestCliTests(unittest.TestCase):
    def test_cli(self):
        with tempfile.TemporaryDirectory(prefix='cxxpython-unittest-cli-') as directory:
            root = Path(directory)
            (root / 'test_sample.py').write_text(
                'import unittest\n'
                'class Example(unittest.TestCase):\n'
                '    def test_ok(self):\n'
                '        self.assertEqual(2 + 2, 4)\n'
                '    def test_failure(self):\n'
                '        self.assertEqual(1, 2)\n', encoding='utf-8')
            (root / 'test_other.py').write_text(
                'import unittest\n'
                'class Other(unittest.TestCase):\n'
                '    def test_ok(self): pass\n', encoding='utf-8')

            scenarios = (
                ('named', ('-v', 'test_sample.Example.test_ok'), 0,
                 b'test_ok (test_sample.Example.test_ok) ... ok', b'OK'),
                ('failure', ('-v', 'test_sample.Example.test_failure'), 1,
                 b'FAIL: test_failure (test_sample.Example.test_failure)',
                 b'FAILED (failures=1)'),
                ('missing', ('-v', 'test_sample.Example.test_missing'), 1,
                 b'ERROR: test_missing (unittest.loader._FailedTest.test_missing)',
                 b'FAILED (errors=1)'),
                ('discovery', ('discover', '-v', '-p', 'test_other.py'), 0,
                 b'test_ok (test_other.Other.test_ok) ... ok', b'OK'),
            )
            for name, args, status, detail, summary in scenarios:
                with self.subTest(scenario=name):
                    reference = self.run_cli(self.reference, root, args, 30)
                    actual = self.run_cli(self.binary, root, args, self.timeout)
                    self.assertEqual(reference.returncode, status, reference.stderr)
                    self.assertEqual(reference.stdout, b'')
                    self.assertIn(detail, reference.stderr)
                    self.assertIn(summary, reference.stderr)
                    self.assertEqual(actual.returncode, reference.returncode, actual.stderr)
                    self.assertEqual(actual.stdout, reference.stdout)
                    self.assertEqual(normalize_timing(actual.stderr),
                                     normalize_timing(reference.stderr))
                    print('PASS unittest CLI', name, flush=True)

    @staticmethod
    def run_cli(binary, cwd, args, timeout):
        return subprocess.run([binary, '-m', 'unittest', *args], cwd=cwd,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                              timeout=timeout)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--reference', type=Path, required=True)
    parser.add_argument('--timeout', type=float, default=30,
                        help='Product process timeout; sanitizer builds use the configured test budget')
    options = parser.parse_args()
    UnittestCliTests.binary = str(options.binary.resolve())
    UnittestCliTests.reference = str(options.reference.resolve())
    UnittestCliTests.timeout = options.timeout
    unittest.main(argv=[__file__])
