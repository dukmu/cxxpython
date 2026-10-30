from contextlib import AsyncExitStack, aclosing, asynccontextmanager

def run(awaitable):
    iterator = awaitable.__await__()
    try:
        next(iterator)
    except StopIteration as exc:
        return exc.value
    raise AssertionError('unexpected suspension')

events = []

@asynccontextmanager
async def resource(name):
    events.append('enter ' + name)
    try:
        yield name
    finally:
        events.append('exit ' + name)

async def callback(name):
    events.append('callback ' + name)

async def suppress(typ, value, tb):
    events.append('suppress ' + typ.__name__)
    return True

stack = AsyncExitStack()
print('entered', run(stack.enter_async_context(resource('a'))))
stack.push_async_callback(callback, 'b')
stack.callback(events.append, 'sync')
moved = stack.pop_all()
run(stack.aclose())
print('before close', events)
run(moved.aclose())
print('after close', events)

stack = AsyncExitStack()
stack.push_async_exit(suppress)
error = ValueError('body')
print('suppressed', run(stack.__aexit__(ValueError, error, None)), events[-1])

class Resource:
    async def aclose(self):
        events.append('closed')

cm = aclosing(Resource())
print('aclosing', isinstance(run(cm.__aenter__()), Resource))
print('aexit', run(cm.__aexit__(None, None, None)), events[-1])
