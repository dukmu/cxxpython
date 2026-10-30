"""Differential tests for public dict and mappingproxy protocols."""

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import unittest


class DictDifferential(unittest.TestCase):
    def execute(self, command):
        result = subprocess.run(command, capture_output=True, text=True, timeout=15)
        self.observations.append(dict(command=command, stdout=result.stdout,
                                      stderr=result.stderr, exit_code=result.returncode))
        return result

    def check_program(self, source, expression):
        actual = self.execute([self.binary, "-c", source, "--eval", expression])
        expected = self.execute([self.reference, "-c", source + "\nprint(repr((" + expression + ")))"])
        self.assertEqual(expected.returncode, 0, expected.stderr)
        self.assertEqual(actual.returncode, 0, actual.stderr)
        self.assertEqual(actual.stdout, expected.stdout)

    def test_public_protocols(self):
        cases = [
            ("d = dict(((1, 'a'), ('x', 2)))\nd[True] = 'b'\nd[1.0] = 'c'\nk = next(iter(d))",
             "len(d), k is 1, d[True], d['x']"),
            ("n = float('nan')\nd = dict(((n, 1),))", "d[n], next(iter(d)) is n"),
            ("d = dict((('a', 1), ('b', 2), ('c', 3)))\nd['a'] = 4\ndel d['b']\nd['b'] = 5\nit = iter(d)",
             "next(it), next(it), next(it), next(it, None), d['a']"),
            ("d = dict(((1, 2),), x=3)\ne = dict(d)\ne.update(((1, 4), ('z', 5)))\n"
             "p = e.pop('x')\ns = e.setdefault('new', 6)\nt = e.setdefault('new', 9)\n"
             "g = e.get('missing', 7)\nc = e.copy()\ne.clear()",
             "d[1], p, s, t, g, c[1], c['z'], len(e)"),
            ("class D(dict):\n    def __missing__(self, key):\n        return 42\nd = D()",
             "d['missing'], d.get('missing'), len(d)"),
            ("d = dict()\nok = False\ntry:\n    hash(d)\nexcept TypeError:\n    ok = True", "ok"),
            ("import sys\nn = sys.__dict__\nn['_dict_test_value'] = 12\na = sys._dict_test_value\n"
             "del n['_dict_test_value']\nsys.modules['_dict_test_alias'] = sys\n"
             "b = sys.modules['_dict_test_alias'] is sys\ndel sys.modules['_dict_test_alias']",
             "a, b"),
            ("class C:\n    x = 1\nv = C.__dict__\nC.x = 2\nC.y = 3\nreadonly = False\n"
             "try:\n    v['x'] = 9\nexcept TypeError:\n    readonly = True",
             "v['x'], v['y'], readonly"),
            ("d = dict()\nclass K:\n    def __init__(self, value):\n        self.value = value\n"
             "    def __hash__(self):\n        return 3\n"
             "    def __eq__(self, other):\n        d['side'] = 8\n        return self.value == other.value\n"
             "a = K(1)\nb = K(2)\nd[a] = 10\nd[b] = 20\nx = d[K(1)]",
             "x, d[b], d['side'], len(d)"),
            ("class H:\n    def __hash__(self):\n        raise ValueError('hash')\n"
             "class E:\n    def __hash__(self):\n        return 5\n"
             "    def __eq__(self, other):\n        raise ValueError('eq')\n"
             "d = dict()\nd[E()] = 1\na = False\nb = False\n"
             "try:\n    d[H()] = 2\nexcept ValueError:\n    a = True\n"
             "try:\n    d[E()]\nexcept ValueError:\n    b = True",
             "a, b, len(d)"),
            ("d = dict((('a', 1), ('b', 2)))\nk = d.keys()\nv = d.values()\ni = d.items()\n"
             "d['a'] = 4\ndel d['b']\nd['c'] = 3\nki = iter(k)\nvi = iter(v)\nii = iter(i)",
             "len(k), len(v), len(i), next(ki), next(ki), next(vi), next(vi), next(ii), next(ii)"),
            ("d = dict((('a', 1), ('b', 2)))\nit = iter(d)\na = next(it)\nd['b'] = 3\n"
             "b = next(it)\nend = next(it, 'end')\nd['c'] = 4\nagain = next(it, 'end')\n"
             "it2 = iter(d)\nnext(it2)\nd['d'] = 5\nchanged = False\n"
             "try:\n    next(it2)\nexcept RuntimeError:\n    changed = True",
             "a, b, end, again, changed"),
            ("d = dict((('a', 1), ('b', 2)))\nit = iter(d)\na = next(it)\ndel d['a']\n"
             "d['c'] = 3\nb = next(it)\nchanged = False\n"
             "try:\n    next(it)\nexcept RuntimeError:\n    changed = True",
             "a, b, changed"),
            ("d = dict((('a', 1), ('b', 2)))\ne = dict((('b', 2), ('a', 1)))\n"
             "f = dict((('a', 1), ('b', 3)))",
             "repr(d), repr(d.keys()), repr(d.values()), repr(d.items()), d == e, d == f, d != f"),
            ("C = type('C', (), dict(((1, 2), ('x', 3))))\np = C.__dict__\nc = dict(p)",
             "p[1], c[1], c['x'], repr(p) == 'mappingproxy(' + repr(c) + ')'"),
            ("import __main__ as main\nclass K:\n    def __hash__(self):\n        return hash('alias')\n"
             "    def __eq__(self, other):\n        return other == 'alias'\n"
             "key = K()\nmain.__dict__[key] = 42\nseen = main.alias\n"
             "def read():\n    return alias\nread_seen = read()\nalias = 43\n"
             "after = main.__dict__[key]\ndel alias\nmissing = main.__dict__.get(key, 'absent')\n"
             "C = type('C', (), dict(((key, 11),)))\nbefore = C.alias\n"
             "C.alias = 12\nchanged = C.__dict__[key]\nkeys = C.__dict__.copy()\n"
             "del C.alias\ngone = C.__dict__.get(key, 'absent')",
             "seen, read_seen, after, missing, before, changed, keys[key], gone"),
        ]
        for source, expression in cases:
            with self.subTest(source=source, expression=expression):
                self.check_program(source, expression)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True)
    parser.add_argument("--reference", required=True)
    parser.add_argument("--report", type=Path)
    args, remaining = parser.parse_known_args()
    DictDifferential.binary = args.binary
    DictDifferential.reference = args.reference
    observations = []
    version = subprocess.check_output([args.reference, "--version"], text=True).strip()
    print("Reference:", version, flush=True)
    loader = unittest.TestLoader()
    suite = (loader.loadTestsFromNames(remaining, module=__import__(__name__)) if remaining
             else loader.loadTestsFromTestCase(DictDifferential))

    def attach_observations(test):
        if isinstance(test, unittest.TestSuite):
            for child in test:
                attach_observations(child)
        else:
            test.observations = observations

    attach_observations(suite)
    result = unittest.TextTestRunner().run(suite)
    if args.report:
        provenance = {}
        for label, command in (("binary", args.binary), ("reference", args.reference)):
            path = Path(shutil.which(command) or command).resolve()
            provenance[label] = dict(path=str(path), sha256=hashlib.sha256(path.read_bytes()).hexdigest())
        args.report.write_text(json.dumps(dict(
            captured_at=datetime.now(timezone.utc).isoformat(), reference_version=version,
            provenance=provenance, passed=result.wasSuccessful(), observations=observations), indent=2) + "\n")
    raise SystemExit(not result.wasSuccessful())
