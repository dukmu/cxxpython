events = []
class Delegate:
    def __iter__(self): return self
    def __next__(self): return 1
    def throw(self, *args):
        events.append(args)
        return 2
def f():
    yield from Delegate()
g = f()
next(g)
payload = ValueError('a', 'b')
value = g.throw(payload)
print(repr((value, events[0][0] is payload, events[0][0].args)))
