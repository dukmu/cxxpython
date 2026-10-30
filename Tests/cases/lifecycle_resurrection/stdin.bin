import gc, _weakref
gc.disable()
saved = []
events = []
class C:
    def __del__(self):
        saved.append(self)
        events.append('del')
        gc.collect()
obj = C()
obj.cycle = obj
old = _weakref.ref(obj)
del obj
gc.collect()
state = old() is None, gc.is_finalized(saved[0])
new = _weakref.ref(saved[0])
saved.clear()
gc.collect()
print(state, events, new() is None)
