import gc
from _weakref import ref
from itertools import tee

class Item:
    pass

def source():
    for _ in range(120):
        yield Item()

a, b = tee(source())
first = next(a)
reference = ref(first)
del first
print(reference() is not None)
for _ in range(60):
    next(a)
for _ in range(61):
    next(b)
gc.collect()
print(reference() is None)
