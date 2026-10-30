# Behavioral cases adapted from CPython 3.12.14 Lib/test/test_copy.py (PSF license).
import copy
import copyreg


def check(name, action):
    try:
        action()
    except Exception as exc:
        print(name, type(exc).__name__, str(exc))
    else:
        print(name, 'ok')


def custom_copy():
    class Example:
        def __init__(self, value):
            self.value = value

        def __copy__(self):
            return Example(self.value)

        def __deepcopy__(self, memo):
            return Example(copy.deepcopy(self.value, memo))

        def __reduce_ex__(self, protocol):
            raise AssertionError('copy hooks must precede reduction')

    original = Example([1])
    assert copy.copy(original).value is original.value
    result = copy.deepcopy(original)
    assert result.value == original.value and result.value is not original.value


def reduce_precedence():
    events = []

    class Example:
        def __reduce_ex__(self, protocol):
            events.append(protocol)
            return 'identity'

        def __reduce__(self):
            raise AssertionError('reduce_ex must take precedence')

    original = Example()
    assert copy.copy(original) is original
    assert copy.deepcopy(original) is original
    assert events == [4, 4]

    class Legacy:
        def __reduce__(self):
            events.append('reduce')
            return 'identity'

    original = Legacy()
    assert copy.copy(original) is original
    assert copy.deepcopy(original) is original
    assert events == [4, 4, 'reduce', 'reduce']


def registry():
    class Example:
        def __new__(cls, value):
            result = object.__new__(cls)
            result.value = value
            return result

        def __reduce_ex__(self, protocol):
            raise AssertionError('registry must precede reduction')

    def reducer(original):
        return Example, (original.value,)

    copyreg.pickle(Example, reducer, Example)
    try:
        original = Example([2])
        shallow = copy.copy(original)
        deep = copy.deepcopy(original)
        assert shallow is not original and shallow.value is original.value
        assert deep is not original and deep.value is not original.value
        assert deep.value == original.value
    finally:
        del copyreg.dispatch_table[Example]


def state_hooks():
    class Example:
        def __init__(self, value):
            self.value = value

        def __getstate__(self):
            return self.value

        def __setstate__(self, state):
            self.value = state

    for value in ([3], 0.0):
        original = Example(value)
        shallow = copy.copy(original)
        deep = copy.deepcopy(original)
        assert shallow.value is value
        assert deep.value == value
        if isinstance(value, list):
            assert deep.value is not value


def default_state():
    class GetState:
        def __getstate__(self):
            return {'value': self.value}

    class SetState:
        def __setstate__(self, state):
            self.value = state['value']

    for kind in (GetState, SetState):
        original = kind()
        original.value = [4]
        shallow = copy.copy(original)
        deep = copy.deepcopy(original)
        assert shallow.value is original.value
        assert deep.value == original.value and deep.value is not original.value


def slots():
    class Base:
        __slots__ = ('__private',)

        def store(self, value):
            self.__private = value

        def load(self):
            return self.__private

    class Example(Base):
        __slots__ = ('value', '__dict__')

    original = Example()
    original.value = [5]
    original.extra = original.value
    original.store(original.value)
    shallow = copy.copy(original)
    deep = copy.deepcopy(original)
    assert shallow.value is original.value and shallow.load() is original.value
    assert deep.value == original.value and deep.value is not original.value
    assert deep.extra is deep.value and deep.load() is deep.value


def newargs():
    class Example(int):
        def __new__(cls, value):
            result = int.__new__(cls)
            result.value = value
            return result

        def __getnewargs__(self):
            return (self.value,)

    original = Example([6])
    shallow = copy.copy(original)
    deep = copy.deepcopy(original)
    assert type(shallow) is Example and shallow.value is original.value
    assert type(deep) is Example and deep.value == original.value
    assert deep.value is not original.value


def newargs_ex():
    class Example(int):
        def __new__(cls, *, value):
            result = int.__new__(cls)
            result.value = value
            return result

        def __getnewargs_ex__(self):
            return (), {'value': self.value}

        def __getnewargs__(self):
            raise AssertionError('getnewargs_ex must take precedence')

    original = Example(value=[7])
    shallow = copy.copy(original)
    deep = copy.deepcopy(original)
    assert type(shallow) is Example and shallow.value is original.value
    assert type(deep) is Example and deep.value == original.value
    assert deep.value is not original.value


def iterator_reducers():
    class Sequence(list):
        def __reduce__(self):
            return Sequence, (), self.__dict__, iter(self)

    class Mapping(dict):
        def __reduce__(self):
            return Mapping, (), self.__dict__, None, iter(self.items())

    for original, key in ((Sequence([[8]]), 0), (Mapping(value=[8]), 'value')):
        original.extra = original[key]
        shallow = copy.copy(original)
        deep = copy.deepcopy(original)
        assert type(shallow) is type(original) and type(deep) is type(original)
        assert shallow[key] is original[key] and shallow.extra is original.extra
        assert deep[key] == original[key] and deep[key] is not original[key]
        assert deep.extra is deep[key]


def errors():
    class BadState:
        def __getstate__(self):
            raise ValueError('state failure')

    class NoReduction:
        def __getattribute__(self, name):
            if name.startswith('__reduce'):
                raise AttributeError(name)
            return object.__getattribute__(self, name)

    class SixTuple:
        def __reduce__(self):
            return SixTuple, (), {}, None, None, None

    for operation in (copy.copy, copy.deepcopy):
        for value, kind, message in ((BadState(), ValueError, 'state failure'),
                                     (NoReduction(), copy.Error, None),
                                     (SixTuple(), TypeError, None)):
            try:
                operation(value)
            except kind as exc:
                if message is not None:
                    assert str(exc) == message
            else:
                raise AssertionError('expected copy failure')


check('custom copy', custom_copy)
check('reduce precedence', reduce_precedence)
check('registry', registry)
check('state hooks', state_hooks)
check('default state', default_state)
check('slots', slots)
check('getnewargs', newargs)
check('getnewargs_ex', newargs_ex)
check('iterator reducers', iterator_reducers)
check('errors', errors)
