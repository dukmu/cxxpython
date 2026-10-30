import sys

class Sized:
    def __init__(self, size):
        self.size = size
    def __sizeof__(self):
        return self.size

small = Sized(7)
large = Sized(107)
print(sys.getsizeof(small) - small.__sizeof__() ==
      sys.getsizeof(large) - large.__sizeof__())
print(sys.getsizeof(large) - sys.getsizeof(small))
small.__sizeof__ = lambda: 1000
print(sys.getsizeof(small) == sys.getsizeof(Sized(7)))
print(sys.getsizeof([]) >= [].__sizeof__())
print(sys.getsizeof(object()) >= object().__sizeof__())

class Bad:
    def __init__(self, value):
        self.value = value
    def __sizeof__(self):
        if self.value == 'raise':
            raise TypeError('size failure')
        if self.value == 'other':
            raise RuntimeError('other failure')
        return self.value

marker = object()
for value in ('raise', None):
    print(sys.getsizeof(Bad(value), marker) is marker)
    try:
        sys.getsizeof(Bad(value))
    except TypeError:
        print('TypeError')
for value in (-1, 1 << 100, 'other'):
    try:
        sys.getsizeof(Bad(value), marker)
    except (ValueError, OverflowError, RuntimeError) as exc:
        print(type(exc).__name__)
print(sys.getsizeof(object=Bad(None), default=None) is None)

class IndexOnly:
    def __index__(self):
        return 4
print(sys.getsizeof(Bad(IndexOnly()), marker) is marker)

class Descriptor:
    def __get__(self, instance, owner):
        return lambda: 23
class UsesDescriptor:
    __sizeof__ = Descriptor()
print(sys.getsizeof(UsesDescriptor()) == sys.getsizeof(Sized(23)))
