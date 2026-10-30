import _weakref as weakref
import gc

events = []
class Target:
    def __init__(self):
        self.data = [1, 2, 3]
        self.value = 7
    def __str__(self):
        return 'target'
    def __len__(self):
        return len(self.data)
    def __iter__(self):
        return iter(self.data)
    def __getitem__(self, index):
        gc.collect()
        return self.data[index]
    def __setitem__(self, index, value):
        self.data[index] = value
    def __delitem__(self, index):
        del self.data[index]

target = Target()
p = weakref.proxy(target)
print(type(p) is weakref.ProxyType, p.__class__ is Target, isinstance(p, Target))
print(weakref.proxy(target) is p, str(p), len(p), list(p))
p.value = 9
p[1] = 8
del p[0]
print(target.value, p[0], 3 in p)
print(repr(p).startswith('<weakproxy at '), ' to Target at ' in repr(p))
try:
    hash(p)
except TypeError:
    print('unhashable')
with_callback = weakref.proxy(target, lambda value: events.append(type(value).__name__))
print(weakref.getweakrefcount(target), callable(p))
del target
gc.collect()
print(events)
for action in (lambda: p.value, lambda: str(p), lambda: len(p), lambda: p == p):
    try:
        action()
    except ReferenceError:
        print('ReferenceError')

class Callable:
    def __call__(self, value, *, scale=2):
        gc.collect()
        return value * scale
target = Callable()
q = weakref.proxy(target)
print(type(q) is weakref.CallableProxyType, callable(q), q(3, scale=4))
