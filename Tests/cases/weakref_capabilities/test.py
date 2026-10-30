import _weakref, importlib
original = _weakref.ref
importlib.reload(_weakref)
class C: pass
class Empty: __slots__ = ()
class Slotted: __slots__ = ('__weakref__',)
class Tuple(tuple): pass
class List(list): pass
def gen(): yield 1
values = (object, type, int, C, C(), Empty(), Slotted(),
          1, 1.0, '', b'', (), [], {}, set(), frozenset(),
          Tuple(), List(), gen, gen.__code__, gen())
result = []
for value in values:
    try:
        ref = _weakref.ref(value)
        result.append(ref() is value)
    except TypeError:
        result.append(False)
print(original is _weakref.ref, result)
