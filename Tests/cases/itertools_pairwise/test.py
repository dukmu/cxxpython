from itertools import pairwise

print(list(pairwise([])))
print(list(pairwise([1])))
print(list(pairwise('ABCD')))
p = pairwise([1, 2, 3])
print(iter(p) is p, next(p), next(p))
print(next(p, 'done'), next(p, 'done'))

class Broken:
    def __iter__(self):
        return self
    def __next__(self):
        raise ValueError('broken')

p = pairwise(Broken())
try:
    next(p)
except ValueError as e:
    print(type(e).__name__, str(e))
print(next(p, 'done'))
