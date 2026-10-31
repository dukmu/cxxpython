import gc
import random

r = random.Random(2468)
state = r.getstate()
first = (r.random(), r.getrandbits(40), r.gauss(0, 1))
r.setstate(state)
print('restore', first == (r.random(), r.getrandbits(40), r.gauss(0, 1)))
cached = r.getstate()
next_gauss = r.gauss(0, 1)
r.setstate(cached)
print('gauss cache', next_gauss == r.gauss(0, 1))


class Index:
    def __index__(self):
        gc.collect()
        return 7


print('index callback', 0 <= r.randrange(Index()) < 7)
for label, function, args in [
    ('seed', r.seed, (object(),)),
    ('range', r.randrange, (0,)),
    ('step', r.randrange, (1, 4, 0)),
    ('choice', r.choice, ([],)),
    ('sample', r.sample, ([1, 2], 3)),
    ('weights', r.choices, ([1, 2], [0, 0])),
    ('bits', r.getrandbits, (-1,)),
    ('state', r.setstate, ((999, (), None),)),
]:
    try:
        function(*args)
    except (ValueError, TypeError, IndexError) as exc:
        print(label, type(exc).__name__)
    else:
        raise AssertionError(label)
