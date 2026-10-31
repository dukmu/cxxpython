import gc
import math

for n in (0, 1, 2, 3, 4, 15, 16, 17, 2**64 - 1, 2**64, 10**200 + 123):
    r = math.isqrt(n)
    print(r * r <= n < (r + 1) * (r + 1))

class Index:
    def __index__(self):
        gc.collect()
        return 81

print(math.isqrt(Index()))
for value in (-1, 1.5, "4"):
    try:
        math.isqrt(value)
    except (ValueError, TypeError) as exc:
        print(type(exc).__name__)

class BadIndex:
    def __index__(self):
        gc.collect()
        return 1.5

try:
    math.isqrt(BadIndex())
except TypeError:
    print("bad index")
