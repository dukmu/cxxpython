from itertools import groupby, tee

g = groupby('aaabbc')
k, first = next(g)
print(k, next(first))
k, second = next(g)
print(k, list(first), list(second))
print([(key, ''.join(values)) for key, values in g])
print([(key, list(values)) for key, values in groupby([1, 3, 2, 4, 7], key=lambda x: x % 2)])

def source():
    for value in range(4):
        print('pull', value)
        yield value

a, b = tee(source())
print('created')
print(next(a), next(a), next(b))
c, d = tee(b)
print(list(a), list(c), list(b), list(d))
print(tee(42, 0))
try:
    tee([], -1)
except ValueError as exc:
    print(type(exc).__name__, str(exc))
