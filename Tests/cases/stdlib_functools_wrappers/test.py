# Adapted from CPython 3.12 test_functools.py: partial, wraps, reduce,
# cmp_to_key, partialmethod and total_ordering tests.
from functools import wraps, partial, partialmethod, cmp_to_key, reduce, total_ordering

def f(a, b, c=0):
    'original doc'
    return a + b + c

g = partial(f, 2, c=4)
print(g(3), g.func is f, g.args, g.keywords)
@wraps(f)
def wrapper(*args, **kwargs):
    return f(*args, **kwargs)
print(wrapper(1, 2), wrapper.__name__, wrapper.__doc__, wrapper.__wrapped__ is f)
class Cell:
    def set(self, value, scale=1):
        return value * scale
    double = partialmethod(set, scale=2)
print(Cell().double(5))
print(reduce(lambda x, y: x + y, ['a', 'b', 'c'], ''))
try:
    reduce(lambda x, y: x + y, [])
except TypeError as e:
    print(type(e).__name__, str(e))
print(sorted([1, 4, 2, 3], key=cmp_to_key(lambda a, b: b - a)))
@total_ordering
class Number:
    def __init__(self, n):
        self.n = n
    def __eq__(self, other):
        return self.n == other.n
    def __lt__(self, other):
        return self.n < other.n
print(Number(1) <= Number(1), Number(2) > Number(1), Number(1) >= Number(2))
