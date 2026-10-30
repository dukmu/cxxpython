from itertools import chain, repeat, islice, starmap, pairwise

print(list(chain('AB', [], (3, 4))))
print(list(chain.from_iterable(['ab', (), 'c'])))
print(list(chain()))
print(list(repeat('x', 3)), list(repeat('x', -2)))
r = repeat(object='y', times=2)
print(r.__length_hint__(), next(r), r.__length_hint__(), list(r))
print(list(islice(repeat('z'), 3)))
print(list(islice(range(20), 2, 10, 3)))
print(list(islice(range(5), None, None, None)))
source = iter(range(10))
print(list(islice(source, 2, 5)), next(source))
print(list(starmap(pow, [(2, 3), [3, 2]])))
print(list(pairwise('abc')))

for thunk in (lambda: repeat('x', None), lambda: islice([], -1),
              lambda: islice([], 0, 4, 0), lambda: chain.from_iterable(1)):
    try:
        thunk()
    except Exception as e:
        print(type(e).__name__)

def f(x):
    if x == 1:
        raise ValueError('callback')
    return x * 2
s = starmap(f, [(1,), (2,)])
try:
    next(s)
except ValueError as e:
    print(str(e))
print(next(s), next(s, 'done'))

c = chain.from_iterable([[1], 42, [3]])
print(next(c))
try:
    next(c)
except TypeError:
    print('bad inner')
print(next(c, 'done'))
