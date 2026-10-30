events = []
def source():
    events.append(1)
    return [2, 3]
def make():
    offset = 10
    return (events.append(x) or x + offset for x in source() if x > 2)
g = make()
before = events[:]
value = next(g)
print(repr((before, events, value)))
