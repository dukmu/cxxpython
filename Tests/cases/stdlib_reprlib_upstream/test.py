# Adapted from CPython 3.12 test_reprlib.py: ReprTests and TestRecursiveRepr.
import reprlib

r = reprlib.Repr()
print(r.repr(list(range(20))))
print(r.repr('abcdefghijklmnopqrstuvwxyz' * 4))
r.maxlist = 3
print(r.repr([1, 2, 3, 4, 5]))

class Node:
    def __init__(self):
        self.child = self

    @reprlib.recursive_repr()
    def __repr__(self):
        return 'Node(%r)' % self.child

print(repr(Node()))
