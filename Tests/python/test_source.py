"""Host-side differential tests of source workflows in both actual executables."""

import argparse
from datetime import datetime, timezone
import hashlib
import json
import shutil
import subprocess
import tempfile
from textwrap import dedent
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

    def test_python_importlib_policy(self):
        cases = [
            ("from importlib import util\nfrom importlib.machinery import FileFinder",
             "FileFinder('.').path, FileFinder('').path, "
             "util.spec_from_file_location('relative', './relative.py').origin"),
            ("from importlib.machinery import ModuleSpec\n"
             "a = ModuleSpec('pkg.child', None, origin='memory')\n"
             "b = ModuleSpec('pkg.child', None, origin='memory')",
             "a == b, a.parent, a.has_location, a.cached, repr(a)"),
            ("from importlib.machinery import ModuleSpec\n"
             "from importlib.util import module_from_spec\nimport sys\n"
             "s = ModuleSpec('fresh.child', None, origin='memory')\n"
             "s.has_location = True\ns.cached = 'custom'\nm = module_from_spec(s)",
             "m.__name__, m.__package__, m.__file__, m.__cached__, "
             "m.__spec__ is s, 'fresh.child' in sys.modules"),
            ("from importlib.machinery import ModuleSpec\n"
             "from importlib.util import module_from_spec\n"
             "s = ModuleSpec('pkg', None, is_package=True)\nm = module_from_spec(s)",
             "s.parent, m.__path__ is s.submodule_search_locations, s.has_location"),
            ("import importlib\nimport sys\n"
             "a = importlib.import_module('importlib.util')\n"
             "b = importlib.import_module('.util', 'importlib')",
             "a is b, a is sys.modules['importlib.util'], a.__package__, "
             "a.__loader__ is a.__spec__.loader"),
            ("import importlib\nimport sys\nfrom importlib.machinery import ModuleSpec\n"
             "events = []\nclass Module:\n    pass\n"
             "class Loader:\n"
             "    def create_module(self, spec):\n"
             "        events.append('create')\n        return Module()\n"
             "    def exec_module(self, module):\n"
             "        events.append('exec')\n"
             "        module.value = getattr(module, 'value', 40) + 1\n"
             "class Finder:\n"
             "    def find_spec(self, name, path, target=None):\n"
             "        if name == 'differential_import':\n"
             "            events.append(target is not None)\n"
             "            return ModuleSpec(name, Loader())\n"
             "        return None\n"
             "sys.meta_path.insert(0, Finder())\n"
             "m = importlib.import_module('differential_import')\n"
             "same = importlib.reload(m) is m",
             "events, same, m.value, m.__spec__._initializing if hasattr(m.__spec__, '_initializing') else False"),
            ("e = ModuleNotFoundError('missing', name='child', path='source', name_from='parent')\n"
             "e.args = ('changed',)",
             "e.name, e.path, e.msg, e.name_from, str(e), e.args"),
            ("import sys\nfrom importlib import util\n"
             "sys.modules['plain_parent'] = type(sys)('plain_parent')\n"
             "try:\n    util.find_spec('plain_parent.child')\n"
             "except ModuleNotFoundError as e:\n"
             "    result = (e.name, type(e.__cause__).__name__)\n",
             "result"),
            ("import sys\nfrom importlib import util\n"
             "sys.meta_path = [object()]\n"
             "missing = util.find_spec('not_discoverable')\n"
             "sys.meta_path = None\n"
             "try:\n    util.find_spec('not_discoverable')\n"
             "except ImportError:\n    shutdown = True\n",
             "missing, shutdown"),
        ]
        for source, expression in cases:
            with self.subTest(expression=expression):
                self.check_program(source, expression)

    def test_base_builtins(self):
        cases = [
            ("errors = []\ntry:\n    bytes(9223372036854775807)\n"
             "except OverflowError as e:\n    errors.append((type(e).__name__, str(e)))\n"
             "for text in ['0g', 'g0', '0', '0 ', '00 0', 'z\\xff']:\n"
             "    try:\n        bytes.fromhex(text)\n"
             "    except ValueError as e:\n        errors.append(str(e))", "errors"),
            ("events = []\nclass Flag:\n"
             "    def __bool__(self):\n        events.append('bool')\n        return True\n"
             "class Stream:\n"
             "    def write(self, text): events.append(text)\n"
             "    def flush(self): events.append('flush')\n"
             "print('x', file=Stream(), flush=Flag())", "events"),
            ("events = []\nclass Text(str):\n"
             "    def __str__(self):\n        events.append('str')\n        return 'converted'\n"
             "class Stream:\n"
             "    def __getattribute__(self, name):\n"
             "        events.append(name)\n        return object.__getattribute__(self, name)\n"
             "    def write(self, text): events.append(text)\n"
             "print(Text('x'), Text('y'), sep=Text(':'), end=Text('!'), file=Stream())", "events"),
            ("import sys\nsaved = sys.stdout\nsys.stdout = None\n"
             "print(1, sep=42)\nsys.stdout = saved", "True"),
            ("e = UnicodeEncodeError('ascii', 'x', 0, -9223372036854775808, 'bad')",
             "str(e)"),
            ("x = bytes([0, 127, 128, 255])\nclass B(bytes): pass\nb = B(x)",
             "x, tuple(x), x[::-1], type(b).__name__, bytes(b), "
             "b':'.join([b'a', b'b']), x.hex(':', -2), bytes.fromhex('00 7f 80 ff')"),
            ("class A:\n    def value(self): return 40\n"
             "class B(A):\n    def value(self): return super().value() + 2\n"
             "b = B()\ns = super(B, b)",
             "b.value(), s.value(), s.__self__ is b, s.__self_class__ is B"),
            ("class K:\n    def __abs__(self): return 'custom'\n"
             "class R:\n    def __rdivmod__(self, other): return ('reflected', other)",
             "abs(-2**100), abs(-0.0), abs(K()), all([1, 2]), any([0, 2]), "
             "ord(b'\\xff'), ord(chr(0x10ffff)), callable(K), divmod(-7, 3), divmod(7, R()), "
             "min([3, 1, 2]), max([], default=42), min(1, -2, key=abs), "
             "sum([1e100, 1.0, -1e100]), sum([1, 2], 40)"),
            ("value = 30\ndef f(x):\n"
             "    return locals()['x'], eval('x + value'), globals()['value']\n"
             "g = {'x': 1}\nl = {'x': 40}\n"
             "exec(compile('result = x + 2', 'base.py', 'exec'), g, l)",
             "f(12), l['result'], 'result' in g, eval(compile('x + 2', 'base.py', 'eval'), g, l)"),
            ("class D:\n    def __dir__(self): return ['z', 'a', 'b']\n"
             "d = D()\nd.item = 42", "dir(d), vars(d), vars()['d'] is d"),
            ("print('native', 42, b'\\xff', sep=':', end='!\\n')", "True"),
            ("import sys\nsaved = sys.stdout\nevents = []\n"
             "class Stream:\n    def write(self, text): events.append(text)\n"
             "    def flush(self): events.append('flush')\n"
             "sys.stdout = Stream()\nprint('a', 2, sep=':', end='!', flush=True)\n"
             "sys.stdout = saved", "events"),
            ("text = '\\u4f60\\U0001f600'\nb = text.encode('utf-8')",
             "b, b.decode(), '\\xff'.encode('latin-1'), b'\\xff'.decode('ascii', 'replace')"),
            ("errors = []\nfor data in [b'\\x80', b'\\xe2\\x82', b'\\xed\\xa0\\x80', b'\\xf4\\x90\\x80\\x80']:\n"
             "    try:\n        data.decode()\n"
             "    except UnicodeDecodeError as e:\n"
             "        errors.append((e.encoding, e.object, e.start, e.end, e.reason, str(e)))",
             "errors"),
        ]
        for source, expression in cases:
            with self.subTest(expression=expression):
                self.check_program(source, expression)

    def test_sets_and_comprehensions(self):
        cases = [
            ("class Items:\n"
             "    def __init__(self): self.consumed = 0\n"
             "    def __iter__(self): return self\n"
             "    def __next__(self):\n        self.consumed += 1\n"
             "        if self.consumed == 1: return 1\n"
             "        raise ValueError('must not consume')\n"
             "a = Items()\nb = Items()\ns = {1}\ni = s.intersection(a)\nsubset = s.issubset(b)",
             "i == s, subset, a.consumed, b.consumed"),
            ("rejected = []\nfor method, receiver in [(set.union, frozenset()), "
             "(frozenset.union, set()), (set.issubset, frozenset()), "
             "(frozenset.__or__, set())]:\n"
             "    try:\n        method(receiver, set())\n"
             "    except TypeError:\n        rejected.append(True)\n"
             "    else:\n        rejected.append(False)", "rejected"),
            ("events = []\nclass K:\n"
             "    def __init__(self, n):\n        events.append(('value', n))\n        self.n = n\n"
             "    def __hash__(self):\n        events.append(('hash', self.n))\n        return self.n\n"
             "s = {K(1), *[], K(2), K(3)}", "events"),
            ("s = {1, 2, 2, *range(3, 6)}\nt = {2, 4, 6}",
             "s == set(range(1, 6)), s & t == {2, 4}, s | t == set(range(1, 7)), "
             "s - t == {1, 3, 5}, s ^ t == {1, 3, 5, 6}, "
             "{1, 2} < s, s > {1}, {1, 2} <= s, s >= {1}, s != t"),
            ("events = []\ndef item(n):\n    events.append(n)\n    return n\n"
             "s = {item(1), *[item(2), item(3)], item(1)}\n"
             "set = None", "events, s == {1, 2, 3}, len({4, 4})"),
            ("x = 99\ns = {x % 3 for x in range(10) if x > 1}\n"
             "nested = {(x, y) for x in range(3) for y in range(x) if y < 2}",
             "s == {0, 1, 2}, x, nested == {(1, 0), (2, 0), (2, 1)}"),
            ("def outer(n):\n"
             "    return {x + n for x in range(3)}\n"
             "s = {(last := x) for x in range(4)}",
             "outer(10) == {10, 11, 12}, s == {0, 1, 2, 3}, last"),
            ("s = {1, 2}\na = s\ns |= {3}\ns &= {2, 3}\n"
             "s ^= {1, 2}\ns -= {3}", "s is a, s == {1}"),
            ("class S(set): pass\nclass F(frozenset): pass\n"
             "s = S([1, 2])\ns.extra = 42\nf = F([2, 1])\nf.extra = 43\n"
             "base = frozenset([1, 2])",
             "s == f, s.extra, f.extra, type(s | {3}) is set, "
             "type(f | {3}) is frozenset, frozenset(base) is base, "
             "base.copy() is base, hash(base) == hash(frozenset([2, 1])), "
             "{frozenset([1, 2]): 42}[f]"),
            ("s = {frozenset([1, 2]), 3}\n"
             "present = {1, 2} in s\ns.remove({1, 2})\ns.discard({1, 2})",
             "present, s == {3}, s.isdisjoint([1, 2]), s.issubset([3, 4]), "
             "s.issuperset([3]), s.union([4]) == {3, 4}, "
             "s.intersection([3, 4]) == {3}, s.difference([3]) == set(), "
             "s.symmetric_difference([3, 4]) == {4}"),
            ("events = []\nclass K:\n"
             "    def __hash__(self):\n        events.append('hash')\n        raise ValueError('hash')\n"
             "s = {1}\ntry:\n    s.add(K())\nexcept ValueError as e:\n    error = str(e)",
             "events, error, s == {1}"),
            ("s = {1}\nit = iter(s)\ns.add(2)\n"
             "try:\n    next(it)\nexcept RuntimeError as e:\n    error = str(e)", "error"),
        ]
        for source, expression in cases:
            with self.subTest(expression=expression):
                self.check_program(source, expression)

    def test_print_closed_pipe_is_a_python_error(self):
        if not Path('/proc/self/fd/0').exists():
            self.skipTest('Linux descriptor fixture')
        source = ("import _imp\nimport sys\n_imp._get_source('/proc/self/fd/0')\n"
                  "try:\n    print('closed pipe')\n"
                  "except OSError:\n    sys.stdout = None\n    sys.stderr.write('caught\\n')\n")
        process = subprocess.Popen([self.binary, '-c', source], stdin=subprocess.PIPE,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        process.stdout.close()
        process.stdin.close()
        process.wait(timeout=15)
        error = process.stderr.read()
        process.stderr.close()
        self.assertEqual(process.returncode, 0, error)
        self.assertEqual(error, 'caught\n')

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

    def test_list_displays_and_targets(self):
        cases = [
            ("", "[], [1,], [1, [2, 3]]"),
            ("counter = 0\ndef next_value():\n    global counter\n    counter += 1\n    return counter\nx = [next_value(), [next_value(), next_value()]]", "x, counter"),
            ("[a, (b, c)] = [1, [2, 3]]\ndel [a, (b, c)]\n[] = []\ndel []", "42"),
            ("def f(x=[1, [2]]): return x\na = f()\nb = f()", "a, a is b, [] is not []"),
            ("class C:\n    [__x, (__y,)] = [4, [5]]\n    def get(self): return [self.__x, self.__y]", "C().get()"),
            ("total = 0\nfor [a, b] in [[1, 2], [3, 4]]:\n    total += a * b", "total"),
        ]
        for source, expression in cases:
            with self.subTest(source=source, expression=expression):
                self.check_program(source, expression)
        for source in ["[a] += [1]", "[1] = [2]", "del [1]"]:
            with self.subTest(source=source):
                self.assertNotEqual(self.execute([self.reference, "-c", source]).returncode, 0)
                self.assertNotEqual(self.execute([self.binary, "-c", source]).returncode, 0)

    def test_list_protocols_mutation_and_callbacks(self):
        cases = [
            ("a = [1, 2]\nb = list(a)\nc = list(range(3))", "a, b, a is not b, c, list('ab')"),
            ("a = [1, 2]\na.append(3)\na.insert(-1, 4)\nx = a.pop(0)\na[0] = 8\n"
             "del a[-1]\nb = a.copy()\na.reverse()", "x, a, b, a.count(8), a.index(8)"),
            ("a = [1, 2]\na.extend(a)\nb = a\na += (3, 4)\na *= 2\n"
             "c = a + [9]\nd = 2 * [3]\ne = [4] * -1", "a, b is a, c, d, e"),
            ("a = [1, 2]\nit = iter(a)\nx = next(it)\na.append(3)\ny = next(it)\n"
             "z = next(it)\nend = next(it, None)\na.append(4)\nagain = next(it, None)",
             "x, y, z, end, again, iter(it) is it"),
            ("a = []\na.append(a)\nn = float('nan')\nb = [n]", "repr(a), b == [n], b == b, [1] < [2], [1] == (1,)"),
            ("class L(list):\n    def __iter__(self):\n        return iter((8, 9))\na = L([1])\n"
             "a.extend(a)\nb = a.copy()\na.__init__(a)", "a, b, type(a) is L, type(b) is list"),
            ("events = []\nclass Source:\n"
             "    def __iter__(self):\n        events.append('iter')\n        return iter((1, 2))\n"
             "    def __len__(self):\n        events.append('len')\n        raise TypeError('decline')\n"
             "    def __length_hint__(self):\n        events.append('hint')\n        return 2\na = list(Source())",
             "a, events"),
            ("class Source:\n    def __getitem__(self, i):\n        if i == 3:\n            raise IndexError(i)\n"
             "        return i + 1\na = list(Source())\nb = tuple(Source())", "a, b"),
            ("class NoIter:\n    __iter__ = None\n    def __getitem__(self, i):\n        return i\n"
             "ok = False\ntry:\n    list(NoIter())\nexcept TypeError:\n    ok = True", "ok"),
            ("a = [1]\nclass Source:\n    def __iter__(self):\n        return self\n"
             "    def __next__(self):\n        if len(a) == 1:\n            return 2\n        raise ValueError('next')\n"
             "ok = False\ntry:\n    a.extend(Source())\nexcept ValueError:\n    ok = True", "a, ok"),
            ("a = []\nclass Item:\n    def __repr__(self):\n        a.clear()\n        return 'item'\n"
             "a.extend((Item(), 2))\ntext = repr(a)", "text, a"),
            ("a = []\nclass Item:\n    def __eq__(self, other):\n        a.clear()\n        return True\n"
             "a.extend((Item(), 2))\nresult = a.remove(0)", "result, a"),
            ("a = [1]\nclass Index:\n    def __index__(self):\n        a.clear()\n        a.append(7)\n        return 0\n"
             "x = a[Index()]\na[Index()] = 8", "x, a"),
            ("a = [1]\nok = False\ntry:\n    hash(a)\nexcept TypeError:\n    ok = True\n"
             "a.clear()", "ok, bool(a), a"),
            ("events = []\nclass Right:\n"
             "    def __rmul__(self, other):\n        events.append('rmul')\n        return 99\n"
             "    def __radd__(self, other):\n        events.append('radd')\n        return 88\n"
             "    def __index__(self):\n        events.append('index')\n        return 2\n"
             "a = [1]\nx = a * Right()\na *= Right()\nb = [2]\ny = b + Right()\nb += Right()",
             "x, a, y, b, events"),
            ("events = []\nclass Right:\n"
             "    def __rmul__(self, other):\n        events.append('rmul')\n        return NotImplemented\n"
             "    def __index__(self):\n        events.append('index')\n        return 2\n"
             "a = [1]\nb = a\na *= Right()", "a, b is a, events"),
            ("class L(list):\n    __mul__ = list.__mul__\n"
             "class Right:\n    def __rmul__(self, other): return 99\n    def __index__(self): return 2\n"
             "class Unrelated:\n    __mul__ = list.__mul__\n"
             "bad = False\ntry:\n    Unrelated() * Right()\nexcept TypeError:\n    bad = True",
             "L([1]) * Right(), bad"),
        ]
        for source, expression in cases:
            with self.subTest(source=source, expression=expression):
                self.check_program(source, expression)
        for source, error in [("list(iterable=())", "TypeError"), ("[1][4]", "IndexError"),
                              ("[].pop()", "IndexError"), ("[].index(1)", "ValueError"),
                              ("[].remove(1)", "ValueError"),
                              ("list.__add__([], 1)", "TypeError"),
                              ("[1] * 1000000000000000000000000000000000", "OverflowError")]:
            for binary in (self.binary, self.reference):
                result = self.execute([binary, "-c", source])
                self.assertEqual(result.returncode, 1, result.stderr)
                self.assertIn(error, result.stderr)

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

    def test_exception_control_flow_workflows(self):
        cases = [
            ("""
             events = 0
             try:
                 raise ValueError(7)
             except (TypeError, ValueError) as error:
                 value = error.args[0]
             else:
                 value = 99
             finally:
                 events += 1
             try:
                 error
             except NameError:
                 cleared = True
             """, "value, events, cleared"),
            ("""
             def f():
                 error = 1
                 def read():
                     return error
                 try:
                     raise ValueError
                 except ValueError as error:
                     pass
                 try:
                     read()
                 except NameError:
                     return True
             """, "f()"),
            ("""
             original = ValueError('inner')
             def reraiser():
                 raise
             try:
                 raise original
             except ValueError:
                 try:
                     reraiser()
                 except ValueError as error:
                     same = error is original
                 try:
                     raise TypeError('outer') from None
                 except TypeError as error:
                     saved = error
             """, "same, saved.__context__ is original, saved.__cause__, saved.__suppress_context__"),
            ("""
             cause = TypeError('cause')
             try:
                 raise ValueError(1) from cause
             except ValueError as error:
                 saved = error
             """, "saved.__cause__ is cause, saved.__context__, saved.__suppress_context__"),
            ("""
             try:
                 raise ValueError from TypeError
             except ValueError as error:
                 saved = error
             """, "type(saved) is ValueError, type(saved.__cause__) is TypeError"),
            ("""
             value = 0
             try:
                 try:
                     value = 1
                 except TypeError:
                     value = 99
                 else:
                     raise TypeError(3)
             except TypeError as error:
                 value += error.args[0]
             finally:
                 value += 10
             """, "value"),
            ("""
             original = ValueError(1)
             try:
                 try:
                     raise original
                 except None:
                     pass
             except TypeError as error:
                 saved = error
             """, "saved.__context__ is original"),
            ("""
             def f():
                 try:
                     raise ValueError
                 finally:
                     return 42
             """, "f()"),
            ("""
             error = ValueError()
             other = TypeError()
             error.__context__ = other
             error.__cause__ = other
             error.__suppress_context__ = False
             """, "error.__context__ is other, error.__cause__ is other, error.__suppress_context__"),
            ("""
             error = ValueError()
             error.__cause__ = None
             """, "error.__cause__, error.__suppress_context__"),
            ("""
             value = 0
             try:
                 raise ValueError
             except ():
                 value = 99
             except:
                 value = 42
             """, "value"),
            ("""
             events = 0
             def __new__(cls):
                 global events
                 events = events * 10 + 3
                 return BaseException.__new__(cls)
             Custom = type('Custom', (ValueError,), __new__.__globals__)
             def first():
                 global events
                 events = events * 10 + 1
                 return Custom
             def second():
                 global events
                 events = events * 10 + 2
                 return TypeError
             try:
                 raise first() from second()
             except ValueError as error:
                 saved = error
             """, "events, type(saved) is Custom, type(saved.__cause__) is TypeError"),
            ("""
             def __new__(cls):
                 return 1
             Invalid = type('Invalid', (ValueError,), __new__.__globals__)
             try:
                 raise Invalid
             except TypeError:
                 caught = True
             """, "caught"),
        ]
        for source, expression in cases:
            with self.subTest(expression=expression):
                self.check_program(dedent(source), expression)
        for header, increment in [("for item in range(5):", ""), ("while item < 4:", "            item += 1\n")]:
            for action in ["break", "continue", "return events", "raise TypeError"]:
                source = (
                    "def f():\n    events = 0\n    item = -1\n    try:\n        " + header + "\n" + increment
                    + "            try:\n                if item == 2:\n                    " + action + "\n"
                    "                events = events * 10 + 1\n            finally:\n                events = events * 10 + 2\n"
                    "        else:\n            events = events * 10 + 5\n"
                    "    except TypeError:\n        events = events * 10 + 3\n"
                    "    finally:\n        events = events * 10 + 4\n    return events\n"
                )
                with self.subTest(loop=header, action=action):
                    self.check_program(source, "f()")

    def test_source_context_managers(self):
        manager = dedent("""
            events = 0
            count = 0
            errors = 0
            suppress = False
            fail_enter = 0
            fail_exit = 0
            def __enter__(self):
                global events, count
                count += 1
                self.tag = count
                events = events * 10 + self.tag
                if self.tag == fail_enter:
                    raise ValueError('enter')
                return 4, 5
            def __exit__(self, kind, error, traceback):
                global events, errors
                events = events * 10 + self.tag
                if error is not None:
                    errors += 1
                if self.tag == fail_exit:
                    raise TypeError('exit')
                return suppress
            Manager = type('Manager', (), __enter__.__globals__)
        """)
        for body, expression in [
            ("with (Manager(), Manager() as (a, b),):\n    value = a + b", "value, events, errors"),
            ("with (Manager()) as value:\n    pass", "value, events, errors"),
            ("suppress = True\nwith Manager(), Manager():\n    raise ValueError", "events, errors"),
            ("suppress = True\nvalue = 0\nwith Manager() as (a, b, c):\n    value = 99", "value, events, errors"),
            ("suppress = True\nfail_enter = 2\nwith Manager(), Manager():\n    pass", "events, errors"),
            ("def f():\n    with Manager() as value:\n        return value\nsaved = f()", "saved, events, errors"),
            ("fail_exit = 2\ntry:\n    with Manager(), Manager():\n        raise ValueError\nexcept TypeError as error:\n    saved = error", "events, errors, type(saved.__context__) is ValueError"),
            ("for i in range(3):\n    with Manager():\n        continue", "events, errors"),
        ]:
            with self.subTest(body=body):
                self.check_program(manager + body, expression)

    def test_exception_syntax_and_runtime_errors(self):
        for source in [
            "try: pass",
            "try: pass\nelse: pass\nfinally: pass",
            "try: pass\nexcept: pass\nexcept ValueError: pass",
            "try: pass\nexcept ValueError as x.y: pass",
            "try: pass\nexcept ValueError as __debug__: pass",
            "try: pass\nfinally: break",
        ]:
            with self.subTest(source=source):
                actual = self.execute([self.binary, "-c", source])
                expected = self.execute([self.reference, "-c", source])
                self.assertEqual(actual.returncode, 1, actual.stderr)
                self.assertNotEqual(expected.returncode, 0)
                self.assertIn("SyntaxError", actual.stderr)
                self.assertIn("SyntaxError", expected.stderr)
        for source, error in [
            ("raise", "RuntimeError"), ("raise 1", "TypeError"),
            ("raise ValueError from 1", "TypeError"),
            ("try:\n    raise ValueError\nexcept None:\n    pass", "TypeError"),
            ("with None: pass", "TypeError"),
            ("with (): pass", "TypeError"),
            ("error = ValueError()\nerror.__cause__ = ValueError", "TypeError"),
            ("error = ValueError()\nerror.__context__ = 1", "TypeError"),
            ("error = ValueError()\nerror.__suppress_context__ = 1", "TypeError"),
        ]:
            with self.subTest(source=source):
                actual = self.execute([self.binary, "-c", source])
                expected = self.execute([self.reference, "-c", source])
                self.assertEqual(actual.returncode, 1, actual.stderr)
                self.assertNotEqual(expected.returncode, 0)
                self.assertIn(error, actual.stderr)
                self.assertIn(error, expected.stderr)

    def test_uncaught_exception_chain_rendering(self):
        for source, marker in [
            ("raise ValueError('outer') from TypeError('cause')", "direct cause"),
            ("try:\n    raise TypeError('inner')\nexcept:\n    raise ValueError('outer')", "During handling"),
        ]:
            with self.subTest(source=source):
                actual = self.execute([self.binary, "-c", source])
                expected = self.execute([self.reference, "-c", source])
                self.assertEqual(actual.returncode, 1, actual.stderr)
                self.assertNotEqual(expected.returncode, 0)
                for text in [marker, "ValueError: outer", "TypeError:"]:
                    self.assertIn(text, actual.stderr)
                    self.assertIn(text, expected.stderr)
        source = "try:\n    raise TypeError('inner')\nexcept:\n    raise ValueError('outer') from None"
        actual = self.execute([self.binary, "-c", source])
        self.assertEqual(actual.returncode, 1)
        self.assertNotIn("TypeError", actual.stderr)
        self.assertIn("ValueError: outer", actual.stderr)
        actual = self.execute([self.binary, "-c", "error = ValueError('cycle')\nerror.__context__ = error\nraise error"])
        self.assertEqual(actual.returncode, 1)
        self.assertEqual(actual.stderr.count("ValueError"), 1)

    def test_class_and_decorator_workflows(self):
        cases = [
            ("""
             value = 90
             class Base:
                 'documentation'
                 value = 3
                 def __init__(self, n=4):
                     self.n = n
                 def total(self):
                     return self.n + self.value
             class Child(Base):
                 @staticmethod
                 def scale(n):
                     return n * 2
                 @classmethod
                 def identify(cls):
                     return cls
             instance = Child(7)
             """, "instance.total(), Child.scale(5), instance.identify() is Child, value, Base.__doc__, Child.__module__"),
            ("""
             def factory(x):
                 class C:
                     x = 99
                     def read(self):
                         return x, __class__
                     class Inner:
                         value = x
                 return C
             A = factory(7)
             B = factory(8)
             """, "A.x, A.Inner.value, A().read()[0], B().read()[0], A().read()[1] is A, A.read.__code__ is B.read.__code__"),
            ("""
             def factory(x):
                 class C:
                     nonlocal x
                     x += 1
                     value = x
                     def read(self):
                         return x
                 return C
             C = factory(7)
             """, "C.value, C().read()"),
            ("""
             x = 90
             def factory(x):
                 class C:
                     global x
                     value = x
                     def read(self):
                         return x
                 return C
             C = factory(7)
             """, "C.value, C().read()"),
            ("""
             class A:
                 def make(self):
                     class B:
                         owner = __class__
                         def who(self):
                             return __class__
                     return B
             B = A().make()
             """, "B.owner is A, B().who() is B, B.who.__qualname__"),
            ("""
             class __C:
                 __slots__ = ('__value',)
                 def __init__(self, __value=7):
                     self.__value = __value
                 def __hidden(self):
                     return self.__value
             c = __C()
             """, "c._C__hidden(), __C._C__hidden.__name__, __C._C__hidden.__qualname__, __C.__init__.__code__.co_varnames"),
            ("""
             events = 0
             def decorator(n):
                 global events
                 events = events * 10 + n
                 def apply(value):
                     global events
                     events = events * 10 + n + 2
                     return value
                 return apply
             def default():
                 global events
                 events = events * 10 + 3
                 return 42
             @decorator(1)
             @decorator(2)
             def f(value=default()):
                 return value
             """, "events, f(), f.__code__.co_firstlineno"),
            ("""
             class A: pass
             class B(A): pass
             bases = B.__bases__
             mro = B.__mro__
             B.value = 42
             B.__name__ = 'Renamed'
             """, "B.__bases__ is bases, B.__mro__ is mro, B.__base__ is A, B.__mro__[1] is A, object.__base__"),
            ("""
             events = 0
             class Descriptor:
                 def __set_name__(self, owner, name):
                     global events
                     events = events * 10 + 1
                     self.owner = owner
                 def __get__(self, instance, owner):
                     return self.owner
             class Base:
                 def __init_subclass__(cls, *, flag=2):
                     global events
                     events = events * 10 + flag
             class Alias:
                 def __mro_entries__(self, bases):
                     return (Base,)
             alias = Alias()
             class Child(alias, flag=3):
                 owner = Descriptor()
             """, "Child.owner is Child, Child.__bases__[0] is Base, Child.__orig_bases__[0] is alias, events"),
            ("""
             class C:
                 def __new__(cls):
                     return 17
             """, "C()"),
            ("""
             class Meta(type):
                 @classmethod
                 def __prepare__(cls, name, bases, *, flag):
                     return type.__prepare__(name, bases)
                 def __new__(cls, name, bases, namespace, *, flag):
                     result = type.__new__(cls, name, bases, namespace)
                     result.flag = flag
                     return result
             class C(metaclass=Meta, flag=7):
                 value = 42
             """, "C.flag, C.value, type(C) is Meta"),
            ("""
             class Namespace:
                 def __getitem__(self, key):
                     if key == 'injected':
                         return 17
                     raise KeyError(key)
                 def __setitem__(self, key, value):
                     if key == 'value':
                         self.saved = value
             class Meta(type):
                 @classmethod
                 def __prepare__(cls, name, bases):
                     return Namespace()
                 def __new__(cls, name, bases, namespace):
                     return namespace
             class C(metaclass=Meta):
                 value = injected
             """, "C.saved"),
            ("""
             def meta(name, bases, namespace):
                 return 7
             class C(metaclass=meta):
                 def who(self):
                     return __class__
             """, "C"),
            ("__build_class__ = None\nclass C: pass", "C.__name__"),
            ("""
             class Descriptor:
                 def __get__(self, instance, owner):
                     if instance is None:
                         return self
                     return instance.value
                 def __set__(self, instance, value):
                     instance.value = value
             class C:
                 item = Descriptor()
             c = C()
             c.item = 42
             """, "c.item"),
            ("""
             def factory():
                 class C:
                     def who(self):
                         return __class__
                 return C
             A = factory()
             B = factory()
             """, "A is not B, A.who.__code__ is B.who.__code__, A.who.__closure__ is not B.who.__closure__"),
            ("""
             class C:
                 def f(self, **kwargs):
                     return len(kwargs)
                 def g(self):
                     return self.f(__x=1, _C__x=2)
             """, "C().g()"),
            ("", "str(KeyError('x')), str(KeyError(1, 2)), str(KeyError()), BaseException.__str__(KeyError('x'))"),
            ("", "str(), str(None), str(object=42), str(encoding='utf8'), str(errors='strict')"),
            ("""
             class Text(str):
                 pass
             value = Text(42)
             """, "value == '42', type(value) is Text, type(str(value)) is str"),
        ]
        for source, expression in cases:
            with self.subTest(expression=expression):
                self.check_program(dedent(source), expression)

    def test_class_definition_errors(self):
        for source in [
            "class C: return 1",
            "for i in range(1):\n    class C: break",
            "class C:\n    def f(self, __x, _C__x): pass",
            "@staticmethod\nvalue = 1",
        ]:
            with self.subTest(source=source):
                actual = self.execute([self.binary, "-c", source])
                expected = self.execute([self.reference, "-c", source])
                self.assertEqual(actual.returncode, 1, actual.stderr)
                self.assertNotEqual(expected.returncode, 0)
                self.assertIn("SyntaxError", actual.stderr)
                self.assertIn("SyntaxError", expected.stderr)
        for source, error in [
            ("class A: pass\nclass B(A, A): pass", "TypeError"),
            ("class C(flag=1): pass", "TypeError"),
            ("str(1, object=2)", "TypeError"),
            ("str(1, encoding='utf8')", "TypeError"),
            ("str(encoding=None)", "TypeError"),
            ("class C:\n    def f(self, __x): return __x\nC().f(__x=1)", "TypeError"),
            ("class A: x = 1\nclass B:\n    y = x", "NameError"),
            ("__build_class__(None, 'C')", "TypeError"),
            ("def f(): pass\n__build_class__(f, None)", "TypeError"),
            ("class Meta(type):\n    def __prepare__(name, bases): return 1\nclass C(metaclass=Meta): pass", "TypeError"),
            ("class Alias:\n    def __mro_entries__(self, bases): return None\nclass C(Alias()): pass", "TypeError"),
            ("class M1(type): pass\nclass M2(type): pass\nclass A(metaclass=M1): pass\nclass B(metaclass=M2): pass\nclass C(A, B): pass", "TypeError"),
            ("class Drop(type):\n    def __new__(cls, name, bases, namespace):\n        return type.__new__(cls, name, bases, type.__prepare__(name, bases))\nclass C(metaclass=Drop):\n    def who(self): return __class__", "RuntimeError"),
        ]:
            with self.subTest(source=source):
                actual = self.execute([self.binary, "-c", source])
                expected = self.execute([self.reference, "-c", source])
                self.assertEqual(actual.returncode, 1, actual.stderr)
                self.assertNotEqual(expected.returncode, 0)
                self.assertIn(error, actual.stderr)
                self.assertIn(error, expected.stderr)

    def test_module_objects_native_import_and_main_namespace(self):
        cases = [
            ("marker = object()", "__import__('__main__').marker is marker"),
            ("", "__import__('__main__').__package__, __import__('__main__').__spec__"),
            ("sys = __import__('sys')", "type(sys).__name__, __import__('sys') is sys"),
            ("b = __import__('builtins')", "b.int is int, b.__dict__ is __builtins__"),
            ("Module = type(__import__('sys'))\nm = Module(name='example', doc='document')\nm.answer = 42", "m.__name__, m.__doc__, m.answer, repr(m), m.__dict__ is m.__dict__"),
            ("Module = type(__import__('sys'))\nclass Child(Module): pass\nm = Child('example')", "type(m).__name__, m.__name__"),
            ("Module = type(__import__('sys'))\nm = Module('example')\ndef fallback(name): return name\nm.__getattr__ = fallback", "m.missing"),
            ("try:\n    __import__('absent_cxxpython_module')\nexcept ModuleNotFoundError as error:\n    value = error.name, error.path", "value"),
        ]
        for source, expression in cases:
            with self.subTest(expression=expression):
                self.check_program(source, expression)
        for expression in ("Module.__new__(1)", "Module.__repr__(1)", "Module.__init__(1, 'x')", "__import__(1)", "__import__('sys', level=-1)", "m.__dict__ = ()"):
            source = "Module = type(__import__('sys'))\nm = Module('example')\ntry:\n    " + expression + "\nexcept BaseException as error:\n    failure = type(error).__name__"
            with self.subTest(expression=expression):
                self.check_program(source, "failure")

    def test_file_entry_and_unicode_diagnostics(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "source.py"
            path.write_text("def f(n):\n    return n + 1\n", encoding="utf-8-sig")
            actual = self.execute([self.binary, str(path), "--eval", "f(41)"])
            self.assertEqual(actual.returncode, 0, actual.stderr)
            self.assertEqual(actual.stdout, "42\n")
            (Path(directory) / "sibling.py").write_text("answer = 42\n", encoding="utf-8-sig")
            path.write_text("import sibling\nvalue = sibling.answer\n", encoding="utf-8")
            actual = self.execute([self.binary, str(path), "--eval", "value, __file__"])
            path.write_text("import sibling\nvalue = sibling.answer\nprint(repr((value, __file__)))\n", encoding="utf-8")
            expected = self.execute([self.reference, str(path)])
            self.assertEqual(actual.returncode, 0, actual.stderr)
            self.assertEqual(expected.returncode, 0, expected.stderr)
            self.assertEqual(actual.stdout, expected.stdout)
        actual = self.execute([self.binary, "--eval", "'\\u4f60' + )"])
        self.assertEqual(actual.returncode, 1)
        self.assertIn("SyntaxError", actual.stderr)

    def test_import_statements_and_custom_hook(self):
        alias_key = ("class AliasKey:\n"
                     "    def __init__(self, name):\n        self.name = name\n"
                     "    def __hash__(self):\n        return hash(self.name)\n"
                     "    def __eq__(self, other):\n        return self.name == other\n")
        cases = [
            ("import sys, builtins as b\nfrom sys import (modules as table,)",
             "sys.__name__, b.int is int, table is sys.modules"),
            ("__import__ = None\nimport sys\ndef f():\n    import builtins as b\n    return b.int",
             "sys.__name__, f() is int"),
            ("import builtins\nimport __main__ as main\nold = builtins.__import__\n"
             "def hook(name, globals, locals, fromlist, level):\n"
             "    global captured\n    captured = name, fromlist, level, globals is locals\n"
             "    return main\nbuiltins.__import__ = hook\ntry:\n    import chosen as alias\n"
             "finally:\n    builtins.__import__ = old", "alias is main, captured"),
            ("import builtins\nold = builtins.__import__\n"
             "class Returned:\n    value = 42\n    __all__ = ('value',)\n"
             "def hook(name, globals, locals, fromlist, level):\n    return Returned()\n"
             "builtins.__import__ = hook\ntry:\n    from chosen import *\n"
             "finally:\n    builtins.__import__ = old", "value"),
            ("import builtins\nold = builtins.__import__\nseen = None\nsame = False\n"
             "def hook(name, globals, locals, fromlist, level):\n"
             "    global seen, same\n    same = seen is locals\n    seen = locals\n    return None\n"
             "builtins.__import__ = hook\n"
             "try:\n    def f():\n        import first\n        import second\n    f()\n"
             "finally:\n    builtins.__import__ = old", "same"),
            ("import builtins\nold = builtins.__import__\n"
             "class Names:\n    def keys(self):\n        return iter(('value',))\n"
             "class Returned:\n    __dict__ = Names()\n    value = 42\n"
             "def hook(name, globals, locals, fromlist, level):\n    return Returned()\n"
             "builtins.__import__ = hook\ntry:\n    from chosen import *\n"
             "finally:\n    builtins.__import__ = old", "value"),
            ("import builtins\n" + alias_key +
             "key = AliasKey('__import__')\nold = builtins.__import__\n"
             "del builtins.__dict__['__import__']\nbuiltins.__dict__[key] = old\n"
             "try:\n    import sys\nfinally:\n    del builtins.__dict__[key]\n"
             "    builtins.__dict__['__import__'] = old", "sys.__name__"),
            ("import builtins\n" + alias_key +
             "key = AliasKey('__build_class__')\nold = builtins.__build_class__\n"
             "del builtins.__dict__['__build_class__']\nbuiltins.__dict__[key] = old\n"
             "try:\n    class Created:\n        value = 42\n"
             "finally:\n    del builtins.__dict__[key]\n"
             "    builtins.__dict__['__build_class__'] = old", "Created.value"),
            ("import __main__ as main\n" + alias_key +
             "def fallback(name):\n    return 42\n"
             "main.__dict__[AliasKey('__getattr__')] = fallback\nseen = main.absent", "seen"),
            ("import sys\n" + alias_key +
             "sys.modules[AliasKey('cached_public_module')] = sys\n"
             "import cached_public_module as cached", "cached is sys"),
        ]
        for source, expression in cases:
            with self.subTest(expression=expression):
                self.check_program(source, expression)
        for source in ("import sys,", "from sys import modules,", "from sys import (*)",
                       "def f():\n    from sys import *", "class C:\n    from sys import *"):
            for binary in (self.binary, self.reference):
                result = self.execute([binary, "-c", source])
                self.assertEqual(result.returncode, 1, result.stderr)
                self.assertIn("SyntaxError", result.stderr)

    def test_file_packages_relative_cycles_star_and_path_changes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            files = {
                "pkg.py": "value = 999\n",
                "pkg/__init__.py": "value = 10\n",
                "pkg/child.py": "from . import value\nanswer = value + 32\n__all__ = ('answer',)\n",
                "left.py": "value = 1\nimport right\nanswer = right.value + value\n",
                "right.py": "from left import value as first\nvalue = first + 40\n",
                "one/ns/a.py": "value = 1\n",
                "two/ns/b.py": "value = 2\n",
                "other/extra.py": "value = 8\n",
                "bad.py": "value = )\n",
                "fails.py": "import missing_dependency_for_test\n",
            }
            for name, text in files.items():
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(text, encoding="utf-8")
            setup = "import sys\nsys.path = " + repr((directory, str(root / "one"), str(root / "two"))) + "\n"
            cases = [
                ("import pkg.child as alias\nfrom pkg.child import *\nimport pkg.child",
                 "answer, alias is pkg.child, pkg.value, alias.__package__, alias.__file__"),
                ("import left, right", "left.answer, right.first"),
                ("import ns.a, ns.b\nns.__path__ = " + repr((str(root / "other"),)) + "\nfrom ns import extra",
                 "ns.a.value, ns.b.value, extra.value"),
                ("try:\n    import bad\nexcept SyntaxError:\n    syntax = True\n"
                 "try:\n    from fails import value\nexcept ModuleNotFoundError as error:\n    dependency = error.name",
                 "syntax, dependency"),
                ("sys.path = list(sys.path)\nsaved = sys.path\nsys.path.append(" + repr(str(root / "other")) + ")\n"
                 "import extra\nremoved = sys.path.pop()", "extra.value, saved is sys.path, removed"),
                ("import pkg\nsaved = pkg.__path__\npkg.__path__.append(" + repr(str(root / "other")) + ")\n"
                 "from pkg import extra", "extra.value, saved is pkg.__path__, type(saved) is list"),
            ]
            for source, expression in cases:
                with self.subTest(expression=expression):
                    self.check_program(setup + source, expression)

    def test_cli_argv_is_a_mutable_list(self):
        source = "import sys\nsys.argv.append('tail')"
        expression = "sys.argv, type(sys.argv) is list"
        actual = self.execute([self.binary, "-c", source, "--eval", expression, "alpha", "--flag"])
        expected = self.execute([self.reference, "-c", source + "\nprint(repr((" + expression + ")))", "alpha", "--flag"])
        self.assertEqual(expected.returncode, 0, expected.stderr)
        self.assertEqual(actual.returncode, 0, actual.stderr)
        self.assertEqual(actual.stdout, expected.stdout)
        with tempfile.TemporaryDirectory() as directory:
            script = Path(directory) / "argv_script.py"
            script.write_text(source, encoding="utf-8")
            actual = self.execute([self.binary, str(script), "--eval", expression, "alpha", "--flag"])
            script.write_text(source + "\nprint(repr((" + expression + ")))\n", encoding="utf-8")
            expected = self.execute([self.reference, str(script), "alpha", "--flag"])
            self.assertEqual(expected.returncode, 0, expected.stderr)
            self.assertEqual(actual.returncode, 0, actual.stderr)
            self.assertEqual(actual.stdout, expected.stdout)


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
