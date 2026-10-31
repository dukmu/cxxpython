import gc
import math

events = []

class Float(float):
    def __float__(self):
        raise RuntimeError('float subclass conversion must not be called')

class Tuple(tuple):
    def __iter__(self):
        raise RuntimeError('dist must use tuple contents')

class Indexed:
    def __index__(self):
        gc.collect()
        return 3

print(math.fsum([Float(1.5), Indexed()]))
print(math.hypot(Float(4), Indexed()))
print(math.dist(Tuple([Float(3), Float(4)]), Tuple([0, 0])))

class Int(int):
    def __mul__(self, other):
        events.append('mul')
        gc.collect()
        return Int(int(self) * int(other))
    def __rmul__(self, other):
        events.append('rmul')
        gc.collect()
        return Int(int(other) * int(self))
    def __radd__(self, other):
        events.append('radd')
        gc.collect()
        return Int(int(other) + int(self))
    def __add__(self, other):
        events.append('add')
        gc.collect()
        return Int(int(self) + int(other))

result = math.prod([2, Int(3), 4])
print(result, type(result).__name__, events)
events.clear()
result = math.sumprod([2, Int(3), 4], [5, 6, 7])
print(result, type(result).__name__, events)

class Multiplier:
    def __rmul__(self, other):
        gc.collect()
        raise ArithmeticError('multiply')

for function, args in [(math.prod, ([2, Multiplier()],)),
                       (math.sumprod, ([1, 2], [3, Multiplier()]))]:
    try:
        function(*args)
    except ArithmeticError as error:
        print(str(error))
