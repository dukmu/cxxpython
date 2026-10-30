# Adapted from CPython 3.12.14 Lib/test/test_types.py SimpleNamespaceTests.
import sys

SimpleNamespace = type(sys.implementation)
ns = SimpleNamespace(a=1, b='two')
assert ns.__dict__ == {'a': 1, 'b': 'two'}
assert ns.a == 1
ns.c = 3
ns.__dict__['d'] = 4
assert ns.d == 4 and 'c' in dir(ns) and 'd' in dir(ns)
del ns.a
assert 'a' not in ns.__dict__
assert repr(ns) == "namespace(b='two', c=3, d=4)"
assert ns.__reduce__() == (SimpleNamespace, (), ns.__dict__)
ns.__init__(c=5, e=6)
assert ns.c == 5 and ns.e == 6
try:
    ns.__dict__ = {}
except AttributeError:
    pass
else:
    raise AssertionError('__dict__ assignment succeeded')
try:
    SimpleNamespace(1)
except TypeError as exc:
    assert str(exc) == 'no positional arguments expected'
else:
    raise AssertionError('positional constructor argument accepted')
try:
    hash(ns)
except TypeError:
    pass
else:
    raise AssertionError('namespace is hashable')
print('simple namespace basic ok')
