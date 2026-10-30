events = []
def f():
    events.append('started')
    x = yield 1
    return (x, 3)
g = f()
before = events[:]
try:
    g.send(9)
except TypeError:
    pass
first = next(g)
try:
    g.send(2)
except StopIteration as e:
    result = e.value
try:
    next(g)
except StopIteration as e:
    exhausted = e.value
print(repr((before, events, first, result, exhausted, g.gi_frame)))
