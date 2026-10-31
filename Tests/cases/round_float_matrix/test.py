# Boundary/precision matrix following CPython 3.12.14 test_float round cases.
values = (0.0, -0.0, 0.1, -0.1, 0.7, 1.125, 1.375, 2.675, 2.685,
          5e-324, -5e-324, 2.2250738585072014e-308,
          1.7976931348623157e308, -1.7976931348623157e308)
precisions = (-1000, -309, -308, -307, -20, -1, 0, 1, 2, 15, 16, 22, 307,
              308, 322, 323, 324, 1000)
for value in values:
    results = []
    for precision in precisions:
        try:
            results.append(round(value, precision))
        except OverflowError:
            results.append('overflow')
    print(value, results)
