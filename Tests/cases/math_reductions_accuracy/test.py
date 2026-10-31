# Focused cases from CPython 3.12.14 Lib/test/test_math.py (PSF license).
import math

for values in [[], [-0.0], [0.1] * 10, [1e100, 1.0, -1e100],
               [1e16, 1.0, 1e-16], [2.0 ** 53, -0.5, -(2.0 ** -54)],
               [2.0 ** 53, 1.0, 2.0 ** -100],
               [1e100, 1.0, -1e100, 1e-100, 1e50, -1, -1e50],
               [5e-324] * 17]:
    print('fsum', math.fsum(values), math.fsum(reversed(values)))
values = [2.0 ** n - 2.0 ** (n + 50) + 2.0 ** (n + 52)
          for n in range(-1074, 972, 2)] + [-(2.0 ** 1022)]
print('partials', math.fsum(values))

print('prod', math.prod([]), math.prod(range(1, 10)), math.prod([1, 2.0, 3, 4]))
print('exact prod', math.prod([2 ** 200, 3, -(2 ** 300)]) == -(3 * 2 ** 500))
print('start', math.prod([2, 3], start=2 ** 100) == 6 * 2 ** 100)
marker = object()
print('empty start identity', math.prod([], start=marker) is marker)
print('generic sequence product', math.prod([2, 3], start='ab'))

print('sumprod', math.sumprod([], []), math.sumprod([10, 20, 30], [1, 2, 3]))
print('dot rounding', math.sumprod([0.1] * 10, [1] * 10))
print('dot bools', math.sumprod([0.1] * 20, [True, False] * 10))
print('dot cancellation', math.sumprod([1.0, 1e101, 1.0, -1e101], [1.0] * 4))
print('product residual', math.sumprod([1.0 + 2.0 ** -27, -1.0],
                                     [1.0 - 2.0 ** -27, 1.0]))
print('exact dot', math.sumprod([2 ** 200, -(2 ** 201)], [2 ** 300, 2 ** 299]) == 0)
print('integer overflow', math.sumprod([2 ** 62] * 4, [1] * 4) == 2 ** 64)
print('mixed fallback', math.sumprod((-5, -5, 10), (1.5, 2 ** 62, 2 ** 61)))
print('complex dot', math.sumprod([1 + 2j, 3 - 4j], [2, 1j]))

for values in [(), (-0.0,), (3, 4), (1.5, 1.5, 0.5), (3e-300, 4e-300),
               (3e300, 4e300), (3 * 5e-324, 4 * 5e-324), (1e308, 1e308),
               (1e308, 1e308, 1e308, 1e308), (1.0,) * 33]:
    print('norm', math.hypot(*values), math.dist(values, [0] * len(values)))
for x, y in [(571282083.1077366, 1180442073.5893514),
             (660269908.2921143, 858574474.0450439)]:
    print('norm correction', math.hypot(x, y))
