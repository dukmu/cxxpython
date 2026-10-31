# CPython 3.12 constructs 10**-ndigits even when the answer is provably zero.
# This VM must bound the exponent by the existing value's bit length first.
precision = -(10**100)
for value in (0, 123, -124, 1 << 4096, -(1 << 4096)):
    assert round(value, precision) == 0
print('bounded integer rounding', 5)
