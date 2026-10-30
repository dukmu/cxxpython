import gc
import weakref

class Item:
    pass
events = []
item = Item()
finalizer = weakref.finalize(item, events.append, 'called')
print(finalizer.alive, finalizer.atexit, finalizer.peek()[0] is item)
finalizer()
print(finalizer.alive, finalizer(), events)

item = Item()
finalizer = weakref.finalize(item, events.append, 'detached')
info = finalizer.detach()
print(info[0] is item, info[1] == events.append, info[2], info[3], finalizer.alive)
del info, item
gc.collect()
print(events)

item = Item()
item.cycle = item
finalizer = weakref.finalize(item, events.append, 'cycle')
del item
gc.collect()
print(finalizer.alive, events)
