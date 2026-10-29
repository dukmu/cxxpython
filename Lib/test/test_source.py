"""Run supported source workflows through both actual executables."""

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
