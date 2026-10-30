# Adapted from CPython 3.12 test_functools.py: TestCache, TestLRU,
# and TestCachedProperty.
from functools import cache, lru_cache, cached_property

@cache
def fib(n):
    if n < 2:
        return n
    return fib(n - 1) + fib(n - 2)
print([fib(n) for n in range(8)])
print(fib.cache_info())
fib.cache_clear()
print(fib.cache_info())

@lru_cache(maxsize=2)
def square(n):
    return n * n
print(square(1), square(2), square(1), square(3), square(2))
print(square.cache_info())

class Item:
    def __init__(self):
        self.calls = 0
    @cached_property
    def cost(self):
        self.calls += 1
        return self.calls * 2
item = Item()
print(item.cost, item.cost, item.calls)
del item.cost
print(item.cost, item.calls)
