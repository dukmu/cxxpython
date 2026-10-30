def ignores():
    try:
        yield 1
    except GeneratorExit:
        yield 2
g = ignores()
next(g)
try:
    g.close()
except RuntimeError as e:
    message = str(e)
suspended = g.gi_suspended
g.close()
def escapes():
    raise StopIteration('oops')
    yield 1
try:
    next(escapes())
except RuntimeError as e:
    cause = e.__cause__
    linked = e.__context__ is cause
print(repr((message, suspended, str(cause), linked)))
