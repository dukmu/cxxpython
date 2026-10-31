import gc
import math

methods = (int.__trunc__, int.__floor__, int.__ceil__)
for value in (0, 1, -1, True, False, (1 << 4096) + 17, -(1 << 4096) + 17):
    results = [method(value) for method in methods]
    results += [math.trunc(value), math.floor(value), math.ceil(value)]
    assert all(type(result) is int and result == value for result in results)
print('exact integer rounding')


class Integer(int):
    def __int__(self):
        raise AssertionError('must use native integer contents')


value = Integer((1 << 4096) + 17)
assert all(type(method(value)) is int and method(value) == value for method in methods)
assert math.floor(value) == value and math.ceil(value) == value and math.trunc(value) == value
print('subclass native conversion')


class Override(Integer):
    def __floor__(self):
        gc.collect()
        return 'floor'

    def __ceil__(self):
        gc.collect()
        return 'ceil'

    def __trunc__(self):
        gc.collect()
        return 'trunc'


value = Override(3)
print('dispatch', math.floor(value), math.ceil(value), math.trunc(value))
for method in methods:
    for receiver in (1.5, '1', object()):
        try:
            method(receiver)
        except TypeError:
            print('invalid receiver')
        else:
            raise AssertionError(receiver)
