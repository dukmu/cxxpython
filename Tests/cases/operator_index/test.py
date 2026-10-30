import gc
import operator

assert operator.index(42) == 42
assert type(operator.index(True)) is int
assert operator.index(2**200) == 2**200


class Int(int):
    def __index__(self):
        raise AssertionError('native integer payload must be used')


assert type(operator.index(Int(7))) is int
assert operator.index(Int(7)) == 7
events = []


class Descriptor:
    def __get__(self, obj, owner):
        gc.collect()
        events.append(owner.__name__)
        return lambda: 9


class Index:
    __index__ = Descriptor()


assert operator.index(Index()) == 9
assert events == ['Index']
Index.__index__ = lambda self: 10
assert operator.index(Index()) == 10
del Index.__index__
item = Index()
item.__index__ = lambda: 11
try:
    operator.index(item)
except TypeError:
    pass
else:
    assert False


class Broken:
    def __index__(self):
        gc.collect()
        raise ValueError('callback failure')


try:
    operator.index(Broken())
except ValueError as error:
    assert str(error) == 'callback failure'
print('native index protocol ok')
