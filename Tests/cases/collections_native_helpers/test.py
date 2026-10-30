from _collections import _count_elements, _tuplegetter

counts = {'a': 5}
print(_count_elements(counts, 'abaca'))
print(sorted(counts.items()))
events = []
class Mapping(dict):
    def get(self, key, default=None):
        events.append(('get', key, default))
        return super().get(key, default)
    def __setitem__(self, key, value):
        events.append(('set', key, value))
        super().__setitem__(key, value)

m = Mapping()
_count_elements(m, 'aba')
print(events, sorted(m.items()))
class Pair(tuple):
    first = _tuplegetter(0, 'first element')
    last = _tuplegetter(-1, 'last element')

p = Pair((10, 20))
print(p.first, Pair.first.__doc__)
try:
    print(p.last)
except IndexError as exc:
    print(type(exc).__name__, str(exc))
try:
    p.first = 3
except AttributeError as exc:
    print(type(exc).__name__, str(exc))
