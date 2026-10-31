import gc
import math

for n in [1, 2, 10, 2 ** 1023, (2 ** 1024) - 1, 2 ** 4096,
          (2 ** 4096) + (2 ** 4043), (2 ** 4096) + (3 * 2 ** 4043)]:
    print(math.log(n), math.log2(n), math.log10(n), math.log(n, 2))
print(math.log(64, 2), math.log(0.25, 0.5), math.log(math.inf, math.inf))

class Number:
    def __init__(self, label, value):
        self.label = label
        self.value = value
    def __float__(self):
        print(self.label)
        gc.collect()
        return self.value

print(math.log(Number('numerator', 8.0), Number('base', 2.0)))
for args in [(0,), (-1,), (1, 1), (2, 0), (2, None), ('3',)]:
    try:
        math.log(*args)
    except (ValueError, ZeroDivisionError, TypeError) as error:
        print(type(error).__name__)

class IntSubclass(int):
    def __float__(self):
        raise RuntimeError('must not call __float__')

print(math.log2(IntSubclass(8)))
