# CPython 3.12.14 test_float and test_long round cases: ties, precision,
# binary/decimal conversion, huge ndigits, infinities and signed zero.
for value in (0.5, 1.5, 2.5, -0.5, -1.5, -2.5, 1e30):
    print('float integer', value, round(value), type(round(value)).__name__)
for value, digits in ((2.675, 2), (2.685, 2), (1.25, 1), (1.35, 1),
                      (-0.01, 0), (1.2345, 3), (1e-320, 322),
                      (1e308, -307), (1.0, 10**100), (-1.0, -(10**100))):
    print('float digits', value, digits if abs(digits) < 1000 else 'huge', round(value, digits))
for value in (15, 25, 35, -15, -25, -35, 123456789012345678901234567890):
    print('int', value, round(value, -1), round(value, -2))
print('extreme int', round(123, -10000), round(123, 10**100))
value = 10**40
print('identity', round(value) is value, round(value, 0) is value,
      round(value, 1) is value, round(value, -1) is value)
print('booleans', round(True), round(False), type(round(True)).__name__)
for text in ('nan', 'inf', '-inf'):
    value = float(text)
    print('special precision', text, round(value, 2))
    try:
        round(value)
    except (ValueError, OverflowError) as exc:
        print('special integer', type(exc).__name__)
try:
    round(1.79e308, -308)
except OverflowError:
    print('overflow')
