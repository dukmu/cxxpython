import abc
import _abc
import _weakref
import gc

class Meta(type):
    def __hash__(cls):
        return 9
    def __eq__(cls, other):
        return isinstance(other, Meta)

class X(metaclass=Meta):
    pass

class Y(metaclass=Meta):
    pass

class Root(abc.ABC):
    pass

Root.register(X)
print(issubclass(Y, Root))
print(len(_abc._get_dump(Root)[0]))
ref = _weakref.ref(X)
del X
gc.collect()
print(ref() is None)
print(len(_abc._get_dump(Root)[0]))

class UnhashableMeta(type):
    __hash__ = None

class Unhashable(metaclass=UnhashableMeta):
    pass

try:
    issubclass(Unhashable, Root)
except TypeError:
    print('unhashable')
