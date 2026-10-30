from itertools import compress, filterfalse, takewhile, dropwhile

print(list(compress('ABCDEF', [1, 0, 1, 0, 1, 1])))
print(list(compress('abcd', [1, 0])))
print(list(filterfalse(None, [0, 1, '', 'x', False])))
print(list(filterfalse(lambda x: x % 2, range(6))))
print(list(takewhile(lambda x: x < 4, [1, 3, 4, 2])))
print(list(dropwhile(lambda x: x < 4, [1, 3, 4, 2])))

def predicate(x):
    if x == 2:
        raise ValueError('predicate')
    return x < 3

d = dropwhile(predicate, [1, 2, 3, 1])
try:
    next(d)
except ValueError as e:
    print(str(e))
print(list(d))
t = takewhile(predicate, [1, 2, 3, 1])
print(next(t))
try:
    next(t)
except ValueError as e:
    print(str(e))
print(list(t))
