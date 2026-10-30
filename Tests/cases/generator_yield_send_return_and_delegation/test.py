def inner():
    x = yield 1
    return x
def outer():
    result = yield from inner()
    yield result
g = outer()
first = next(g)
second = g.send(42)
print(repr((first, second)))
