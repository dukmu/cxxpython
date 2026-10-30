events = []
def f():
    try:
        try:
            yield 1
        except ValueError as e:
            yield str(e)
    finally:
        events.append('closed')
g = f()
next(g)
value = g.throw(ValueError('bad'))
g.close()
h = f()
h.close()
print(repr((value, events, g.gi_frame, h.gi_frame)))
