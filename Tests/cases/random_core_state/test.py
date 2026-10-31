import _random
import gc

r = _random.Random(42)
state = r.getstate()
print(len(state), state[-1])
sample = [r.getrandbits(129), r.random(), r.getrandbits(7)]
r.setstate(state)
print(sample == [r.getrandbits(129), r.random(), r.getrandbits(7)])

class Index:
    def __init__(self, value):
        self.value = value
    def __index__(self):
        gc.collect()
        return self.value

class Broken:
    def __index__(self):
        gc.collect()
        raise RuntimeError('bad index')

print(r.getrandbits(Index(12)) >= 0)
state = r.getstate()
cases = [None, (), state[:-1] + (625,), state[:10] + (-1,) + state[11:],
         state[:10] + (Broken(),) + state[11:]]
for value in cases:
    try:
        r.setstate(value)
    except Exception as exc:
        print(type(exc).__name__, r.getstate() == state)
    else:
        print('missing exception')
for value in [-1, 1 << 80, 1.5, Broken()]:
    try:
        r.getrandbits(value)
    except Exception as exc:
        print(type(exc).__name__)
    else:
        print('missing exception')

class Child(_random.Random):
    pass

print(Child(123).getrandbits(128) == _random.Random(123).getrandbits(128))
