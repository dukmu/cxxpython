def f():
    yield 1
    yield 2
g = f()
next(g)
try:
    g.throw(object())
except TypeError:
    rejected = g.gi_suspended
value = next(g)
print(repr((rejected, value)))
