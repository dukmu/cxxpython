import sys
events = []
class Delegate:
    def __iter__(self): return self
    def __next__(self): return 1
    @property
    def close(self): raise LookupError('close lookup')
delegate = Delegate()
sys.unraisablehook = lambda a: events.append((str(a.exc_value), a.object is delegate))
def gen():
    try:
        yield from delegate
    finally:
        events.append('finally')
g = gen()
next(g)
result = g.close()
print(result, events, g.gi_frame)
