from collections.abc import Hashable, Iterable, Iterator, Sequence, Set

class Iterates:
    def __iter__(self):
        return iter(())

class NotIterable:
    __iter__ = None

print(isinstance(Iterates(), Iterable), issubclass(Iterates, Iterable))
print(isinstance(NotIterable(), Iterable), issubclass(NotIterable, Iterable))
print(isinstance(iter([]), Iterator), isinstance([], Sequence))
print(isinstance(set(), Set), isinstance(frozenset(), Set))
print(isinstance(1, Hashable), isinstance([], Hashable))
