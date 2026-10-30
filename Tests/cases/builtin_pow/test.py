import gc

print(pow(2, 10), pow(2, -2), pow(2.0, 3), pow(2, 3.0))
print(pow(base=3, exp=4, mod=7), pow(3, 4, None))
print(pow(3, -1, 11), pow(3, -2, -11), pow(-2, 5, -7))
print(pow(0, -1, 1), pow(0, 0, -1))
print(pow(2, 1000, 123456789012345678901234567890))
for values in ((2, 3, 0), (2, -1, 4)):
    try:
        pow(*values)
    except ValueError as error:
        print(type(error).__name__, str(error))
class Base:
    def __pow__(self, exponent, modulus=None):
        gc.collect()
        return ('power', exponent, modulus)
print(pow(Base(), 3), pow(Base(), 3, 7))
class Reflected:
    def __rpow__(self, other):
        gc.collect()
        return ('reflected', other)
print(pow(2, Reflected()))
try:
    pow(2, Reflected(), 3)
except TypeError:
    print('no ternary reflected')
print(int.__pow__(2, 3, 5), int.__rpow__(3, 2, 5))
