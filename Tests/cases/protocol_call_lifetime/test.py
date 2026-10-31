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


events = []


class MultiplyDescriptor:
    def __get__(self, instance, owner):
        events.append('bind multiply')
        owner.__mul__ = lambda self, other: 'new multiply'
        gc.collect()

        def multiply(other):
            gc.collect()
            events.append('multiply')
            assert instance.value == 'receiver' and other.value == 'argument'
            raise ValueError(instance.value + ':' + other.value)

        return multiply


class Multiplier(Payload):
    __mul__ = MultiplyDescriptor()


try:
    Multiplier('receiver') * Payload('argument')
    assert False
except ValueError as error:
    assert str(error) == 'receiver:argument'
assert Multiplier('ignored') * Payload('ignored') == 'new multiply'
assert events == ['bind multiply', 'multiply']
print('multiplication exception:', events)


events = []


class SetDescriptor:
    def __get__(self, instance, owner):
        events.append('bind set')
        owner.__setitem__ = lambda self, key, value: events.append('new set')
        gc.collect()

        def set_item(key, value):
            gc.collect()
            events.append('set')
            assert instance.value == 'container'
            assert key.value == 'key' and value.value == 'updated'

        return set_item


class GetDescriptor:
    def __get__(self, instance, owner):
        events.append('bind get')
        owner.__getitem__ = lambda self, key: 'new get'
        gc.collect()

        def get_item(key):
            gc.collect()
            events.append('get')
            assert instance.value == 'container' and key.value == 'key'
            return Item('original')

        return get_item


class InplaceDescriptor:
    def __get__(self, instance, owner):
        events.append('bind inplace')
        owner.__iadd__ = lambda self, other: 'new inplace'
        gc.collect()

        def inplace(other):
            gc.collect()
            events.append('inplace')
            assert instance.value == 'original' and other.value == 'operand'
            Container.__setitem__ = SetDescriptor()
            return Payload('updated')

        return inplace


class Item(Payload):
    __iadd__ = InplaceDescriptor()


class Container(Payload):
    __getitem__ = GetDescriptor()

    def __setitem__(self, key, value):
        raise AssertionError('in-place operation must replace setitem')


def make_value(value):
    events.append(value)
    return Payload(value)


def make_container():
    events.append('container')
    return Container('container')


make_container()[make_value('key')] += make_value('operand')
assert events == ['container', 'key', 'bind get', 'get', 'operand',
                  'bind inplace', 'inplace', 'bind set', 'set']
assert Container('ignored')[Payload('ignored')] == 'new get'
container = Container('ignored')
container[Payload('ignored')] = Payload('ignored')
assert events[-1] == 'new set'
print('subscript inplace:', events)


class IndependentTruth:
    __bool__ = gc.isenabled


assert bool(IndependentTruth()) is gc.isenabled()
print('native function special: no implicit receiver')


class ForeignInteger:
    __add__ = int.__add__


try:
    ForeignInteger() + 1
    assert False
except TypeError:
    print('transplanted native descriptor: TypeError')
