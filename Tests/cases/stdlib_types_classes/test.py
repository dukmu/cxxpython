# Adapted from CPython 3.12.14 Lib/test/test_types.py ClassCreationTests.
import types

class A:
    pass

class C:
    def __mro_entries__(self, bases):
        return (A,)

c = C()
bases = (c,)
D = types.new_class('D', bases, {}, lambda ns: ns.update(x=42))
assert D.__bases__ == (A,)
assert D.__orig_bases__ is bases
assert types.get_original_bases(D) is bases
assert D.x == 42
unchanged = (A,)
assert types.resolve_bases(unchanged) is unchanged

class Bad:
    def __mro_entries__(self, bases):
        return A

try:
    types.resolve_bases((Bad(),))
except TypeError as exc:
    assert str(exc) == '__mro_entries__ must return a tuple'
else:
    raise AssertionError('non-tuple MRO entries accepted')

def meta(name, bases, namespace, **kw):
    return name, bases, namespace, kw

settings = {'metaclass': meta, 'x': 0}
assert types.new_class('X', (int, object), settings) == ('X', (int, object), {}, {'x': 0})
assert settings == {'metaclass': meta, 'x': 0}
print('types classes ok')
