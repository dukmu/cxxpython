import gc, _weakref
gc.disable()
events = []
class C:
    def __del__(self):
        events.append(('del', a() is None, gc.is_finalized(self)))
obj = C()
obj.cycle = obj
a = _weakref.ref(obj, lambda r: events.append(('first', r() is None)))
b = _weakref.ref(obj, lambda r: events.append(('last', r.__callback__ is None)))
del obj
gc.collect()
print(events, a() is None, b() is None)
