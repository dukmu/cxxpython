class Pause:
    def __await__(self):
        value = yield 'pause'
        return value

async def values():
    try:
        received = yield 1
        print('received', received)
        result = await Pause()
        yield result
    except ValueError as error:
        yield str(error)
    finally:
        print('finally')

def step(request, value=None):
    try:
        print('suspend', request.send(value))
    except StopIteration as error:
        print('value', error.value)
    except StopAsyncIteration:
        print('exhausted')
    except Exception as error:
        print(type(error).__name__, str(error))

g = values()
print(g.__aiter__() is g, g.__name__, g.ag_running, g.ag_await is None)
step(g.__anext__())
r = g.asend(7)
step(r)
print('running', g.ag_running, g.ag_await is not None)
step(g.__anext__())
step(r, 9)
print('running', g.ag_running)
step(g.athrow(ValueError('caught')))
step(g.__anext__())
print('frame', g.ag_frame is None)

g = values()
step(g.__anext__())
step(g.aclose())
step(g.__anext__())

g = values()
r = g.asend(None)
step(r)
step(r)
step(g.aclose())
