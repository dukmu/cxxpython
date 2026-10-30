# CPython 3.12.14 long/float/complex/unicode/bytes __getnewargs__ contracts
# (PSF license), exercised through the unchanged upstream copy implementation.
import copy
import gc
import struct


def failure(kind, action):
    try:
        action()
    except kind:
        return
    raise AssertionError('expected ' + kind.__name__)


def arguments(base, value):
    result = base.__getnewargs__(value)
    assert type(result) is tuple
    expected = (float, float) if base is complex else (base,)
    assert tuple(type(item) for item in result) == expected
    return result


def snapshot(base, value):
    result = arguments(base, value)
    if base in (float, complex):
        return tuple(struct.pack('>d', item) for item in result)
    return result


def boundary_values():
    values = ((int, (0, 42, -1, -(1 << 63), 1 << 64, -(1 << 257))),
              (float, (0.0, -0.0, 1.5, 5e-324, float('inf'),
                       float('-inf'), float('nan'))),
              (complex, (complex(0.0, -0.0), complex(-0.0, 0.0),
                         complex(1.5, -2.5), complex(float('inf'), float('nan')))),
              (str, ('', 'plain', 'a\x00\u1234\U0001f600\udcff')),
              (bytes, (b'', b'plain', b'a\x00\x80\xff')))
    for base, items in values:
        class Example(base):
            pass

        for value in items:
            original = Example(*arguments(base, value))
            expected = snapshot(base, value)
            assert snapshot(base, original) == expected
            for operation in (copy.copy, copy.deepcopy):
                result = operation(original)
                assert type(result) is Example and result is not original
                assert snapshot(base, result) == expected
        print(base.__name__, 'values ok')
    assert arguments(int, True) == (1,)
    assert arguments(int, False) == (0,)


class IntNoConversion(int):
    def __int__(self):
        raise AssertionError('int conversion')

    def __index__(self):
        raise AssertionError('int index')


class FloatNoConversion(float):
    def __float__(self):
        raise AssertionError('float conversion')


class ComplexNoConversion(complex):
    def __complex__(self):
        raise AssertionError('complex conversion')

    @property
    def real(self):
        raise AssertionError('real attribute')

    @property
    def imag(self):
        raise AssertionError('imag attribute')


class StrNoConversion(str):
    def __str__(self):
        raise AssertionError('str conversion')

    def __iter__(self):
        raise AssertionError('str iteration')

    def __getitem__(self, key):
        raise AssertionError('str indexing')


class BytesNoConversion(bytes):
    def __bytes__(self):
        raise AssertionError('bytes conversion')

    def __iter__(self):
        raise AssertionError('bytes iteration')

    def __getitem__(self, key):
        raise AssertionError('bytes indexing')


families = ((int, IntNoConversion, 42),
            (float, FloatNoConversion, -0.0),
            (complex, ComplexNoConversion, complex(-1.5, 2.5)),
            (str, StrNoConversion, 'a\x00\u1234'),
            (bytes, BytesNoConversion, b'a\x00\xff'))


def conversion_bypass_and_state():
    for base, kind, value in families:
        original = kind(value)
        original.payload = [1, {'value': [2]}]
        original.alias = original.payload
        original.owner = original
        expected = snapshot(base, value)
        assert snapshot(base, original) == expected
        shallow = copy.copy(original)
        deep = copy.deepcopy(original)
        assert type(shallow) is kind and type(deep) is kind
        assert snapshot(base, shallow) == snapshot(base, deep) == expected
        assert shallow.payload is original.payload and shallow.alias is original.payload
        assert shallow.owner is original
        assert deep.payload == original.payload and deep.payload is not original.payload
        assert deep.alias is deep.payload and deep.owner is deep
        assert deep.payload[1] is not original.payload[1]
        graph = copy.deepcopy([original, original])
        assert graph[0] is graph[1] and graph[0] is not original
        assert graph[0].owner is graph[0]
        print(base.__name__, 'conversion bypass and state ok')


def collecting_state_hooks():
    for base, _, value in families:
        events = []

        class Example(base):
            def __getstate__(self):
                events.append('getstate')
                gc.collect()
                return {'payload': self.payload}

            def __setstate__(self, state):
                events.append('setstate')
                gc.collect()
                self.payload = state['payload']

        original = Example(value)
        original.payload = [3]
        shallow = copy.copy(original)
        deep = copy.deepcopy(original)
        assert shallow.payload is original.payload
        assert deep.payload == original.payload and deep.payload is not original.payload
        assert snapshot(base, shallow) == snapshot(base, deep) == snapshot(base, original)
        assert events == ['getstate', 'setstate', 'getstate', 'setstate']
        print(base.__name__, 'collecting state hooks ok')


def reconstruction_overrides():
    for base, _, value in families:
        class CustomArguments(base):
            def __getnewargs__(self):
                gc.collect()
                return arguments(base, value)

        class CustomArgumentsEx(base):
            def __getnewargs_ex__(self):
                gc.collect()
                return arguments(base, value), {}

            def __getnewargs__(self):
                raise AssertionError('getnewargs_ex takes precedence')

        class CustomReduction(base):
            def __reduce_ex__(self, protocol):
                assert protocol == 4
                gc.collect()
                return CustomReduction, arguments(base, value), {'payload': self.payload}

            def __getnewargs__(self):
                raise AssertionError('custom reduction takes precedence')

        for kind in (CustomArguments, CustomArgumentsEx, CustomReduction):
            original = kind(value)
            original.payload = [4]
            shallow = copy.copy(original)
            deep = copy.deepcopy(original)
            assert type(shallow) is kind and type(deep) is kind
            assert snapshot(base, shallow) == snapshot(base, deep) == snapshot(base, value)
            assert shallow.payload is original.payload
            assert deep.payload == original.payload and deep.payload is not original.payload

        class Broken(base):
            def __getnewargs__(self):
                gc.collect()
                raise ValueError('newargs failure')

        failure(ValueError, lambda: copy.copy(Broken(value)))
        failure(ValueError, lambda: copy.deepcopy(Broken(value)))
        print(base.__name__, 'reconstruction overrides ok')


def invalid_receivers():
    for base, _, value in families:
        failure(TypeError, lambda: base.__getnewargs__(object()))
        failure(TypeError, lambda: base.__getnewargs__(base(value), 1))
        failure(TypeError, lambda: base.__getnewargs__(self=base(value)))
    print('invalid receivers and arguments ok')


boundary_values()
conversion_bypass_and_state()
collecting_state_hooks()
reconstruction_overrides()
invalid_receivers()
