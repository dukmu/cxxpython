events = []
class Error(Exception):
    def __init__(self, *args):
        events.append('constructed')
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
first = g.throw(Error)
second = g.throw(42)
print(repr((first, second, len(events), events[0][0] is Error, len(events[0]), events[1])))
