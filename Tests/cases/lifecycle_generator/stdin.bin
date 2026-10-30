import gc, _weakref
gc.disable()
events = []
def gen():
    try:
        value = yield 1
        yield 2
    finally:
        events.append('closed')
        gc.collect()
g = gen()
next(g)
g.send(g)
ref = _weakref.ref(g)
del g
gc.collect()
print(events, ref() is None)
