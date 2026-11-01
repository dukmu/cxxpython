import collections
import gc
import io
import operator
import warnings


class IntSubclass(int):
    def __index__(self):
        raise AssertionError('int subclass must use its stored value')


class Index:
    def __init__(self, value, action=None):
        self.value = value
        self.action = action

    def __index__(self):
        gc.collect()
        if self.action is not None:
            self.action()
        return self.value


class Failure:
    def __index__(self):
        gc.collect()
        raise ValueError('index callback failed')


def raises(expected, operation):
    try:
        operation()
    except expected as error:
        print(type(error).__name__, str(error))
    else:
        raise AssertionError('expected ' + expected.__name__)


value = 1000
assert operator.index(value) is value
assert type(operator.index(True)) is int
assert operator.index(True) == 1
subclass = IntSubclass(2)
result = operator.index(subclass)
assert type(result) is int and result == 2 and result is not subclass
assert [10, 20, 30][subclass] == 30
assert [10, 20][True] == 20
result = operator.index(Index(value))
assert type(result) is int and result is value

with warnings.catch_warnings(record=True) as caught:
    warnings.simplefilter('always', DeprecationWarning)
    for returned in (IntSubclass(3), True):
        result = operator.index(Index(returned))
        assert type(result) is int and result == int(returned)
        assert result is not returned
    for warning in caught:
        print(warning.category.__name__, str(warning.message))
    assert len(caught) == 2

raises(ValueError, lambda: operator.index(Failure()))
raises(TypeError, lambda: operator.index(Index('bad')))
raises(TypeError, lambda: [1, 2][Index(1.5)])

items = [10, 20, 30]
assert items[Index(1, lambda: items.insert(0, 5))] == 10
assert items == [5, 10, 20, 30]
items.insert(Index(2, lambda: items.append(40)), 99)
assert items == [5, 10, 99, 20, 30, 40]
assert items.pop(Index(-1, lambda: items.append(50))) == 50
assert items == [5, 10, 99, 20, 30, 40]
assert [1, 2] * Index(2) == [1, 2, 1, 2]
assert [1, 2] * Index(-2) == []
assert (10, 20, 30)[Index(-1)] == 30
assert 'abcd'[Index(2)] == 'c'
assert (1, 2) * Index(2) == (1, 2, 1, 2)
assert 'ab' * Index(2) == 'abab'

huge = 10 ** 100
for index in (huge, -huge):
    raises(IndexError, lambda: [1, 2][Index(index)])
    raises(IndexError, lambda: (1, 2)[Index(index)])
    raises(IndexError, lambda: 'ab'[Index(index)])
    raises(OverflowError, lambda: [1] * Index(index))

assert [0, 1, 2, 3][Index(-huge):Index(huge):Index(2)] == [0, 2]
assert (0, 1, 2, 3)[Index(huge):Index(-huge):Index(-2)] == (3, 1)
assert 'abcd'[Index(-huge):Index(huge)] == 'abcd'
assert slice(Index(-huge), Index(huge), Index(2)).indices(4) == (0, 4, 2)
raises(ValueError, lambda: slice(None, None, Index(0)).indices(4))
items = [1, 2]
raises(OverflowError, lambda: items.insert(Index(huge), 3))
raises(OverflowError, lambda: items.insert(Index(-huge), 0))
assert items == [1, 2]
raises(OverflowError, lambda: items.pop(Index(huge)))
raises(ValueError, lambda: items.pop(Failure()))

events = []
stream = io.BytesIO(b'abcdef')
assert stream.seek(Index(2, lambda: events.append('seek'))) == 2
assert stream.read(1) == b'c'
assert events == ['seek']
raises(ValueError, lambda: stream.seek(Failure()))
raises(OverflowError, lambda: stream.seek(Index(huge)))

queue = collections.deque([10, 20, 30])
assert queue[Index(1, lambda: queue.appendleft(5))] == 10
assert list(queue) == [5, 10, 20, 30]
raises(ValueError, lambda: queue[Failure()])
raises(IndexError, lambda: queue[Index(huge)])

producers = [Index(IntSubclass(2))]
with warnings.catch_warnings():
    warnings.simplefilter('always', DeprecationWarning)

    def collecting_warning(message, category, filename, lineno, file=None, line=None):
        producers[0].value = None
        producers.clear()
        gc.collect()
        print('collecting index warning', category.__name__)

    warnings.showwarning = collecting_warning
    assert [10, 20, 30][producers[0]] == 30

with warnings.catch_warnings():
    warnings.simplefilter('error', DeprecationWarning)
    raises(DeprecationWarning, lambda: operator.index(Index(IntSubclass(4))))

print('native index conversion ok')
