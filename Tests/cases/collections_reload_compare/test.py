import _collections
import importlib

names = ('deque', '_deque_iterator', '_deque_reverse_iterator',
         'defaultdict', '_tuplegetter', '_count_elements')
saved = [getattr(_collections, name) for name in names]
d = _collections.deque([1, 2])
importlib.reload(_collections)
print(all(getattr(_collections, name) is value for name, value in zip(names, saved)))
print(type(d) is _collections.deque, list(d))
class NeverCompare:
    def __eq__(self, other):
        raise AssertionError('different lengths must not compare items')
first = _collections.deque([NeverCompare()])
second = _collections.deque([NeverCompare(), NeverCompare()])
print(first == second, first != second)
