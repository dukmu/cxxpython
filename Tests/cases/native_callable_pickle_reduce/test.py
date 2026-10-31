import _codecs
import gc
import types

for function in [len, _codecs.encode]:
    print(function.__name__, function.__qualname__, function.__reduce__())

for function in [object.__new__, [].append, dict.fromkeys]:
    reduction = function.__reduce__()
    print(function.__qualname__, reduction[0] is getattr, reduction[1][1])
    reconstructed = reduction[0](*reduction[1])
    print(reconstructed.__name__, reconstructed.__self__ is function.__self__)

for descriptor in [list.append, str.join]:
    reduction = descriptor.__reduce__()
    print(descriptor.__name__, descriptor.__qualname__,
          descriptor.__objclass__.__name__, hasattr(descriptor, '__module__'))
    print(reduction[0] is getattr, reduction[1][0] is descriptor.__objclass__,
          reduction[1][1], reduction[0](*reduction[1]) is descriptor)

class List(list):
    pass

value = List()
print(value.append.__qualname__)
List.__qualname__ = 'RenamedList'
print(value.append.__qualname__)

class Receiver:
    def target(self):
        return 'target result'

class Callable:
    @property
    def __name__(self):
        gc.collect()
        return 'target'
    def __call__(self, receiver):
        return None

method = types.MethodType(Callable(), Receiver())
reduction = method.__reduce__()
gc.collect()
print(reduction[0] is getattr, reduction[1][1], reduction[0](*reduction[1])())

class BadName:
    @property
    def __name__(self):
        gc.collect()
        raise LookupError('method name')
    def __call__(self, receiver):
        return None

try:
    types.MethodType(BadName(), Receiver()).__reduce__()
except LookupError as error:
    print(str(error))
