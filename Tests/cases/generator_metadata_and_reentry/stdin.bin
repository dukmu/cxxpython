def f():
    try:
        g.send(None)
    except ValueError as e:
        yield (str(e), g.gi_running)
g = f()
created = (g.gi_running, g.gi_suspended, g.gi_yieldfrom, g.gi_frame is None)
g.__name__ = 'renamed'
g.__qualname__ = 'qualified'
value = next(g)
suspended = (g.gi_running, g.gi_suspended, g.gi_frame is None)
g.close()
print(repr((created, value, suspended, g.__name__, g.__qualname__, g.gi_frame)))
