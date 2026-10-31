import gc
import math

methods = [float.__trunc__, float.__floor__, float.__ceil__]
for value in [0.0, -0.0, 1.25, -1.25, 1.0, -1.0, 5e-324, -5e-324,
              4503599627370495.5, -4503599627370495.5]:
    direct = [method(value) for method in methods]
    print(value, direct, [math.trunc(value), math.floor(value), math.ceil(value)])
    assert all(type(result) is int for result in direct)

for value, expected in [(2.0 ** 1023, 1 << 1023),
                        (float('1.7976931348623157e308'), (1 << 1024) - (1 << 971))]:
    for sign in [1, -1]:
        print([method(sign * value) == sign * expected for method in methods])

for value in [float('nan'), float('inf'), float('-inf')]:
    for method in methods:
        try:
            method(value)
        except (ValueError, OverflowError) as error:
            print(type(error).__name__, str(error))

class Float(float):
    def __float__(self):
        raise RuntimeError('must not convert receiver')
    def __int__(self):
        raise RuntimeError('must not call overridden __int__')

value = Float(-1.25)
print([method(value) for method in methods])
print(math.trunc(value), math.floor(value), math.ceil(value))

class Override(Float):
    def __trunc__(self):
        gc.collect()
        return float.__trunc__(self)
    def __floor__(self):
        gc.collect()
        return float.__floor__(self)
    def __ceil__(self):
        gc.collect()
        return float.__ceil__(self)

value = Override(-(2.0 ** 1023))
for i in range(10):
    results = [math.trunc(value), math.floor(value), math.ceil(value)]
    gc.collect()
    assert all(type(result) is int and result == -(1 << 1023) for result in results)
print('subclass GC integer results retained')

for method in methods:
    for receiver in [1, '1.25', object()]:
        try:
            method(receiver)
        except TypeError:
            print('invalid receiver')
    for arguments in [(), (1.25, 2)]:
        try:
            method(*arguments)
        except TypeError:
            print('invalid arity')
