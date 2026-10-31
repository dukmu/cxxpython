import gc
import weakref

class Item:
    pass

def identity(item):
    yield item

for started in (False, True):
    item = Item()
    reference = weakref.ref(item)
    generator = identity(item)
    frame = generator.gi_frame
    if started:
        next(generator)
    del item
    frame.clear()
    gc.collect()
    print('cleared locals', started, reference() is None, generator.gi_frame is None)
