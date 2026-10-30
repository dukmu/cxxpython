import weakref

class Item:
    pass
item = Item()
weakref.finalize(item, print, 'first finalizer')
weakref.finalize(item, print, 'second finalizer')
ignored = weakref.finalize(item, print, 'disabled finalizer')
ignored.atexit = False
print('main still owns item')
