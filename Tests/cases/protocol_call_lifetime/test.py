import gc
import operator


events = []


class Payload:
    def __init__(self, value):
        self.value = value


class ReplacingCall:
    def __get__(self, instance, owner):
        events.append('bind')
        owner.__call__ = lambda self, *args, **kwargs: 'replacement'
        gc.collect()

        def invoke(arg, suffix='default', *, option='unset'):
            gc.collect()
            assert instance.marker == 'receiver'
            return (arg.value, suffix, option.value)

        return invoke


class Callable:
    __call__ = ReplacingCall()

    def __init__(self):
        self.marker = 'receiver'


callable_value = Callable()
result = callable_value(Payload('argument'), option=Payload('keyword'))
assert result == ('argument', 'default', 'keyword')
assert callable_value(Payload('ignored'), option=Payload('ignored')) == 'replacement'
assert events == ['bind']
print('descriptor:', result, events)


def key(value):
    gc.collect()
    return value.value


values = [Payload(2), Payload(1)]
list.sort(values, key=key, reverse=True)
assert [value.value for value in values] == [2, 1]
bound_sort = list.sort.__get__(values, list)
bound_sort(key=key)
assert [value.value for value in values] == [1, 2]
assert dict.get({'present': 7}, 'missing') is None
assert dict.get({'present': 7}, 'missing', 9) == 9
print('native descriptors:', [value.value for value in values], 'defaults ok')

try:
    list.sort({}, key=key)
    assert False
except TypeError:
    print('native explicit receiver: TypeError')

try:
    list.sort.__get__({}, dict)
    assert False
except TypeError:
    print('native bound receiver: TypeError')


events = []


class Left:
    def __init__(self, value):
        self.value = value

    def __add__(self, other):
        events.append('add')
        gc.collect()
        assert self.value == 'left' and other.value == 'right'
        return NotImplemented

    def __iadd__(self, other):
        events.append('iadd')
        gc.collect()
        assert self.value == 'left' and other.value == 'right'
        return NotImplemented


class Right:
    def __init__(self, value):
        self.value = value

    def __radd__(self, other):
        events.append('radd')
        gc.collect()
        return other.value + '+' + self.value


result = Left('left') + Right('right')
assert result == 'left+right'
assert events == ['add', 'radd']
print('reflected:', result, events)

events = []
value = Left('left')
value += Right('right')
assert value == 'left+right'
assert events == ['iadd', 'add', 'radd']
print('inplace fallback:', value, events)

value = 10 ** 100
assert int(value) is value and operator.index(value) is value


class Integer(int):
    def __index__(self):
        raise AssertionError('integer normalization must use native contents')


converted = operator.index(Integer(value))
assert type(converted) is int and converted == value
assert operator.index(True) == 1 and type(operator.index(True)) is int
print('integer normalization: identity and exact result type')
