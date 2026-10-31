import gc
import math

events = []

class Index:
    def __init__(self, value):
        self.value = value
    def __index__(self):
        gc.collect()
        events.append(self.value)
        return self.value

class Broken:
    def __index__(self):
        gc.collect()
        events.append('broken')
        raise RuntimeError('index failure')

print(math.gcd(Index(12), Index(18)), math.lcm(Index(4), Index(6)))
print(math.comb(Index(12), Index(2)), math.perm(Index(7), Index(2)))
print(math.factorial(Index(6)))
for f, args in [(math.gcd, (1, Broken())), (math.lcm, (0, Broken())),
                (math.comb, (Index(-1), Broken())),
                (math.perm, (Index(-1), Broken()))]:
    try:
        f(*args)
    except RuntimeError as exc:
        print(type(exc).__name__, str(exc))
    else:
        print('missing exception')
print(events)
