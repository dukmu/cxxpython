import gc
from _weakref import ref, proxy, _remove_dead_weakref

class Item:
    pass
item = Item()
live = ref(item)
dead_item = Item()
dead = ref(dead_item)
del dead_item
gc.collect()
values = {'live': live, 'dead': dead, 'other': 3}
print(_remove_dead_weakref(values, 'missing'))
_remove_dead_weakref(values, 'live')
_remove_dead_weakref(values, 'dead')
print(sorted(values), values['live']() is item)
try:
    _remove_dead_weakref(values, 'other')
except TypeError as error:
    print(type(error).__name__, str(error))
class Key:
    def __hash__(self):
        gc.collect()
        return 1
    def __eq__(self, other):
        values[self] = live
        return True
key = Key()
values = {key: dead}
_remove_dead_weakref(values, Key())
print(len(values), values[key] is live)
values = {'proxy': proxy(item)}
del item
gc.collect()
_remove_dead_weakref(values, 'proxy')
print(values)
