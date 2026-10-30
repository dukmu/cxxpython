import gc
import weakref

class Item:
    def method(self, value=3):
        return value + 1

first, second, third = Item(), Item(), Item()
items = weakref.WeakSet([first, second])
print(len(items), first in items, third in items)
print(len(items | [third]), len(items & [second, third]), len(items - [first]))
items ^= [second, third]
print(first in items, second in items, third in items)
del first
gc.collect()
print(len(items), list(items)[0] is third)

callbacks = []
method = weakref.WeakMethod(second.method, lambda ref: callbacks.append(ref() is None))
print(method()(), method().__self__ is second)
del second
gc.collect()
print(method(), callbacks)

reference = weakref.ref(third)
iterator = iter(items)
print(next(iterator) is third)
del third
gc.collect()
print(reference() is not None)
iterator.close()
gc.collect()
print(reference() is None, len(items))
