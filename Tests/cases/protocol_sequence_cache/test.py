import gc


class Right:
    calls = 0

    def __radd__(self, other):
        self.calls += 1
        return "reflected add"

    def __rmul__(self, other):
        self.calls += 1
        return "reflected multiply"


class Derived(list):
    pass


class Copied(list):
    __add__ = list.__add__
    __mul__ = list.__mul__


right = Right()
for i in range(3):
    for left in ([1], Derived([1]), Copied([1])):
        print(left + right, left * right)
        print(left + [2], left * 2)
    gc.collect()


def patched_add(self, other):
    return "patched add"


def patched_mul(self, other):
    return "patched multiply"


Derived.__add__ = patched_add
Derived.__mul__ = patched_mul
print(Derived([1]) + right, Derived([1]) * right)
gc.collect()


def decline(self, other):
    return NotImplemented


Derived.__add__ = decline
Derived.__mul__ = decline
print(Derived([1]) + right, Derived([1]) * right)


class Unrelated:
    __add__ = list.__add__
    __mul__ = list.__mul__


before = right.calls
for i in range(3):
    try:
        Unrelated() + right
    except TypeError:
        print("transplanted add: TypeError")
    try:
        Unrelated() * right
    except TypeError:
        print("transplanted multiply: TypeError")
    gc.collect()
print("transplanted reflected calls:", right.calls - before)
