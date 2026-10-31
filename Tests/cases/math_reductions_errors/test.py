import math

def show(function, *args, **kwargs):
    try:
        print(function(*args, **kwargs))
    except Exception as error:
        print(type(error).__name__)

for values in [[math.inf], [-math.inf], [math.nan, 1],
               [math.inf, -math.inf], [math.nan, math.inf, -math.inf],
               [1e308, 1e308], [2 ** 4000], ['1.5']]:
    show(math.fsum, values)
for values in [[-0.0], [0, math.inf], [math.nan, 0], [1e308, 1e308]]:
    show(math.prod, values)
for p, q in [([1], []), ([], [1]), ([1, 2], [3]),
             ([math.inf, -math.inf], [1.0, 1.0]),
             ([math.nan], [0.0]), ([0.0], [math.inf]),
             ([2 ** 4000], [1.0]), (['bad'], [1.0]),
             ([1e308, 1.0], [1e308, 2.0])]:
    show(math.sumprod, p, q)
for values in [(-0.0,), (math.inf, math.nan), (-math.inf, math.nan),
               (math.nan, 1), (math.inf, 'bad'), (2 ** 4000,)]:
    show(math.hypot, *values)
for p, q in [([], []), ([1], []), ([math.inf], [math.inf]),
             ([math.inf, math.nan], [0, 0]), ([1e308], [-1e308]),
             ([2 ** 4000], [0]), (['bad'], [0])]:
    show(math.dist, p, q)
for function, args, kwargs in [(math.fsum, (), {}), (math.fsum, (), {'seq': []}),
                               (math.prod, ([1], 2), {}), (math.hypot, (), {'x': 3}),
                               (math.sumprod, ([],), {}), (math.dist, (), {'p': [], 'q': []}),
                               (math.prod, (42,), {}), (math.sumprod, ([], None), {})]:
    show(function, *args, **kwargs)
