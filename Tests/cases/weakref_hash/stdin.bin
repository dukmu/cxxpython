import gc, _weakref
gc.disable()
class Target:
    def __hash__(self): return 42
class Ref(_weakref.ref): pass
obj = Target()
obj.cycle = obj
a, b = Ref(obj), Ref(obj)
before = hash(a)
live = a == b
del obj
gc.collect()
try:
    hash(b)
except TypeError as error:
    failure = type(error).__name__
print(before, hash(a), live, a == b, a == a, failure)
class Recursive:
    def __hash__(self):
        if self.entered: return 11
        self.entered = True
        self.inner = hash(ref)
        return 22
target = Recursive()
target.entered = False
ref = _weakref.ref(target)
first = hash(ref)
print(first, target.inner, hash(ref))
