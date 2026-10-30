# CPython 3.12.14 set_reduce, bytearray _common_reduce and tuple_getnewargs
# contracts (PSF license); exercise actual reducers without requiring pickle.
import copy
import gc


def failure(kind, action):
    try:
        action()
    except kind:
        return
    raise AssertionError('expected ' + kind.__name__)


def set_reducers():
    for factory in (set, frozenset):
        for values in ([], [1, 2, 3]):
            original = factory(values)
            reduction = original.__reduce__()
            constructor, arguments, state = reduction
            assert constructor is factory and type(arguments) is tuple
            assert len(arguments) == 1 and type(arguments[0]) is list
            assert sorted(arguments[0]) == values and state is None
            assert constructor(*arguments) == original
            for protocol in range(6):
                result = original.__reduce_ex__(protocol)
                assert result[0] is factory and sorted(result[1][0]) == values
                assert result[2] is None
            deep = copy.deepcopy(original)
            assert type(deep) is factory and deep == original
            if values:
                assert deep is not original


def set_subclasses():
    for base in (set, frozenset):
        class Example(base):
            __slots__ = ('slot', '__dict__')

        original = Example([1, 2])
        original.extra = [3]
        original.slot = original.extra
        constructor, arguments, state = original.__reduce__()
        assert constructor is Example and sorted(arguments[0]) == [1, 2]
        assert state[0] is original.__dict__
        assert state[1] == {'slot': original.slot}
        shallow = copy.copy(original)
        deep = copy.deepcopy(original)
        assert type(shallow) is Example and type(deep) is Example
        assert shallow == deep == original
        assert shallow.extra is original.extra and shallow.slot is original.slot
        assert deep.extra == original.extra and deep.extra is not original.extra
        assert deep.slot is deep.extra


def set_callbacks():
    for base in (set, frozenset):
        events = []

        class Example(base):
            def __iter__(self):
                events.append('iter')
                gc.collect()
                return iter([10, 20])

            def __getstate__(self):
                events.append('state')
                gc.collect()
                return {}

        constructor, arguments, state = Example([1]).__reduce__()
        assert constructor is Example and arguments == ([10, 20],)
        assert state == {} and events == ['iter', 'state']

        class Broken(base):
            def __getstate__(self):
                raise ValueError('state failure')

        failure(ValueError, lambda: Broken().__reduce__())

    class Mutable(set):
        def __getstate__(self):
            self.clear()
            gc.collect()
            return None

    original = Mutable([1, 2])
    reduction = original.__reduce__()
    assert not original and sorted(reduction[1][0]) == [1, 2]


def bytearray_protocols():
    for data in (b'', b'a\x00\x80\xff'):
        original = bytearray(data)
        assert original.__reduce__() == original.__reduce_ex__(2)
        assert original.__reduce_ex__() == original.__reduce_ex__(0)
        for protocol in (-1, 0, 1, 2, 3, 4, 5, 99):
            constructor, arguments, state = original.__reduce_ex__(protocol)
            assert constructor is bytearray and state is None
            assert type(arguments) is tuple
            if not data:
                assert arguments == ()
            elif protocol < 3:
                assert type(arguments[0]) is str
                assert arguments == (data.decode('latin-1'), 'latin-1')
            else:
                assert arguments == (data,) and type(arguments[0]) is bytes
            result = constructor(*arguments)
            assert result == original and result is not original

    failure(TypeError, lambda: original.__reduce_ex__('4'))
    failure(TypeError, lambda: original.__reduce_ex__(None))
    failure(TypeError, lambda: original.__reduce_ex__(proto=4))
    failure(OverflowError, lambda: original.__reduce_ex__(2**100))
    failure(OverflowError, lambda: original.__reduce_ex__(-(2**100)))


def bytearray_subclasses():
    class Example(bytearray):
        __slots__ = ('slot', '__dict__')

    original = Example(b'abc')
    original.extra = [1]
    original.slot = original.extra
    for protocol in (2, 4):
        constructor, arguments, state = original.__reduce_ex__(protocol)
        assert constructor is Example and constructor(*arguments) == original
        assert state[0] is original.__dict__
        assert state[1] == {'slot': original.slot}
    shallow = copy.copy(original)
    deep = copy.deepcopy(original)
    assert type(shallow) is Example and type(deep) is Example
    assert shallow == deep == original
    assert shallow.extra is original.extra and shallow.slot is original.slot
    assert deep.extra == original.extra and deep.extra is not original.extra
    assert deep.slot is deep.extra


def bytearray_callbacks():
    events = []

    class Example(bytearray):
        def __getstate__(self):
            events.append('state')
            self.extend(b'c')
            gc.collect()
            return {'marker': [1]}

    original = Example(b'a')

    class Protocol:
        def __index__(self):
            events.append('index')
            original.extend(b'b')
            gc.collect()
            return 4

    constructor, arguments, state = original.__reduce_ex__(Protocol())
    assert constructor is Example and arguments == (b'abc',)
    assert state == {'marker': [1]} and events == ['index', 'state']

    class Broken(bytearray):
        def __getstate__(self):
            raise ValueError('state failure')

    failure(ValueError, lambda: Broken().__reduce__())
    failure(ValueError, lambda: Broken().__reduce_ex__(4))


def tuple_arguments():
    for original in ((), (1, 2), ([1],)):
        arguments = original.__getnewargs__()
        assert type(arguments) is tuple and len(arguments) == 1
        assert arguments[0] is original

    class Example(tuple):
        def __iter__(self):
            raise AssertionError('getnewargs must use stored tuple elements')

        def __getitem__(self, key):
            raise AssertionError('getnewargs must not call subclass slicing')

    original = Example(([1], 2))
    arguments = original.__getnewargs__()
    assert type(arguments[0]) is tuple and arguments[0] == ([1], 2)
    assert arguments[0][0] is tuple.__getitem__(original, 0)


def tuple_copy():
    class Example(tuple):
        pass

    original = Example(([1], 2))
    original.extra = original[0]
    shallow = copy.copy(original)
    deep = copy.deepcopy(original)
    assert type(shallow) is Example and type(deep) is Example
    assert shallow == deep == original
    assert shallow is not original and shallow[0] is original[0]
    assert shallow.extra is original.extra
    assert deep is not original and deep[0] is not original[0]
    assert deep.extra is deep[0]

    class Arguments(tuple):
        def __getnewargs__(self):
            return ((10, 20),)

    original = Arguments((1, 2))
    assert copy.copy(original) == (10, 20)
    assert copy.deepcopy(original) == (10, 20)


def overrides_and_receivers():
    for base in (set, frozenset, bytearray, tuple):
        class Example(base):
            def __reduce_ex__(self, protocol):
                assert protocol == 4
                return 'identity'

        original = Example()
        assert copy.copy(original) is original
        assert copy.deepcopy(original) is original

    for base in (set, frozenset):
        class Legacy(base):
            def __reduce__(self):
                return 'identity'

        original = Legacy()
        assert copy.copy(original) is original
        assert copy.deepcopy(original) is original

    failure(TypeError, lambda: set.__reduce__(frozenset()))
    failure(TypeError, lambda: frozenset.__reduce__(set()))
    failure(TypeError, lambda: bytearray.__reduce__(b''))
    failure(TypeError, lambda: bytearray.__reduce_ex__(b'', 4))
    failure(TypeError, lambda: tuple.__getnewargs__([]))


for name, action in (('set reducers', set_reducers),
                     ('set subclasses', set_subclasses),
                     ('set callbacks', set_callbacks),
                     ('bytearray protocols', bytearray_protocols),
                     ('bytearray subclasses', bytearray_subclasses),
                     ('bytearray callbacks', bytearray_callbacks),
                     ('tuple arguments', tuple_arguments),
                     ('tuple copy', tuple_copy),
                     ('overrides and receivers', overrides_and_receivers)):
    action()
    print(name, 'ok')
