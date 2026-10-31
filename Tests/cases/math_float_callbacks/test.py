import math

class Real:
    def __float__(self):
        return -2.5

class Indexed:
    def __index__(self):
        return 3

class Rounded:
    def __floor__(self):
        return 'floor result'
    def __ceil__(self):
        return 'ceil result'
    def __trunc__(self):
        return 'trunc result'

print(math.modf(Real()), math.frexp(Indexed()))
print(math.ldexp(1.5, 3))
print(math.floor(Rounded()), math.ceil(Rounded()), math.trunc(Rounded()))
for value in ['1.0', 10 ** 400]:
    try:
        math.modf(value)
    except (TypeError, OverflowError) as error:
        print(type(error).__name__)
