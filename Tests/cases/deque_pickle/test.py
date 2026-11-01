from collections import deque
import pickle


for bound in (None, 3, 0):
    source = deque((1, 2, 3, 4), maxlen=bound)
    reduction = source.__reduce__()
    assert len(reduction) == 4
    assert reduction[0] is deque
    assert reduction[1] == (() if bound is None else ((), bound))
    assert reduction[2] is None
    assert iter(reduction[3]) is reduction[3]
    assert list(reduction[3]) == list(source)
print('reduction shape and constructor arguments')


class Child(deque):
    pass


class Slotted(deque):
    __slots__ = ('label',)


class Custom(deque):
    def __getstate__(self):
        return (self.label, self)

    def __setstate__(self, state):
        self.label, self.reference = state


for protocol in range(6):
    for bound in (None, 3, 0):
        source = deque((1, 2, 3, 4), maxlen=bound)
        clone = pickle.loads(pickle.dumps(source, protocol))
        assert type(clone) is deque
        assert clone.maxlen == bound
        assert list(clone) == list(source)

    for bound in (None, 4):
        shared = []
        source = deque((shared, shared), maxlen=bound)
        source.append(source)
        clone = pickle.loads(pickle.dumps(source, protocol))
        assert clone.maxlen == bound
        assert clone[0] is clone[1] and clone[0] is not shared
        assert clone[2] is clone

    source = Child((10, 20), maxlen=3)
    source.label = 'child'
    source.reference = source
    clone = pickle.loads(pickle.dumps(source, protocol))
    assert type(clone) is Child and clone.maxlen == 3
    assert list(clone) == [10, 20] and clone.label == 'child'
    assert clone.reference is clone

    source = Slotted((30,))
    source.label = 'slot'
    clone = pickle.loads(pickle.dumps(source, protocol))
    assert type(clone) is Slotted
    assert list(clone) == [30] and clone.label == 'slot'

    source = Custom((40,), maxlen=2)
    source.label = 'custom'
    clone = pickle.loads(pickle.dumps(source, protocol))
    assert type(clone) is Custom and clone.maxlen == 2
    assert list(clone) == [40] and clone.label == 'custom'
    assert clone.reference is clone
    print('protocol', protocol, 'values maxlen cycles subclass state')


source = deque((1, 2))
reduction = source.__reduce__()
source.append(3)
try:
    next(reduction[3])
except RuntimeError:
    print('live iterator detects mutation')
else:
    raise AssertionError('deque reduction must retain a live iterator')


class BrokenState(deque):
    def __getstate__(self):
        raise LookupError('state failure')


try:
    BrokenState().__reduce__()
except LookupError as error:
    print('state error', str(error))
else:
    raise AssertionError('getstate exception was lost')


class BrokenIterator(deque):
    def __iter__(self):
        raise LookupError('iterator failure')


try:
    BrokenIterator().__reduce__()
except LookupError as error:
    print('iterator error', str(error))
else:
    raise AssertionError('iterator exception was lost')


class ReentrantState(deque):
    def __getstate__(self):
        self.append(2)
        return None


source = ReentrantState((1,))
assert list(source.__reduce__()[3]) == [1, 2]
print('state callback may mutate deque')
