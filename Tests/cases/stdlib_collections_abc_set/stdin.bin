from collections.abc import Set

class MySet(Set):
    def __init__(self, iterable):
        self.contents = set(iterable)

    def __contains__(self, value):
        return value in self.contents

    def __iter__(self):
        return iter(self.contents)

    def __len__(self):
        return len(self.contents)

a = MySet((1, 2, 3))
b = MySet((3, 4, 5))
print(sorted(a & b), sorted(a | b), sorted(a - b), sorted(a ^ b))
print(MySet((1,)) < a, a <= b, a == MySet((3, 2, 1)))
print(a.isdisjoint(MySet((4, 5))), a.isdisjoint(b))
