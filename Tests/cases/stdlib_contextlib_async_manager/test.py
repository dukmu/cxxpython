from contextlib import asynccontextmanager, nullcontext

def run(awaitable):
    iterator = awaitable.__await__()
    try:
        next(iterator)
    except StopIteration as exc:
        return exc.value
    raise AssertionError('unexpected suspension')

events = []

@asynccontextmanager
async def managed(value):
    events.append('enter')
    try:
        yield value
    except ValueError:
        events.append('caught')
    finally:
        events.append('exit')

cm = managed(42)
print('entered', run(cm.__aenter__()))
error = ValueError('body')
print('suppressed', run(cm.__aexit__(ValueError, error, None)), events)
cm = managed(7)
print('normal', run(cm.__aenter__()), run(cm.__aexit__(None, None, None)), events)
cm = nullcontext('async value')
print('null', run(cm.__aenter__()), run(cm.__aexit__(None, None, None)))

@managed('decorator')
async def decorated(value):
    events.append('call ' + str(value))
    if value == 0:
        raise LookupError('failure')
    return value * 2

print('decorator', run(decorated(2)), run(decorated(3)))
try:
    run(decorated(0))
except LookupError as exc:
    print('decorator error', str(exc), events[-3:])

@asynccontextmanager
async def empty():
    if False:
        yield

@asynccontextmanager
async def twice():
    yield 1
    yield 2

try:
    run(empty().__aenter__())
except RuntimeError as exc:
    print('empty', str(exc))
cm = twice()
print('first', run(cm.__aenter__()))
try:
    run(cm.__aexit__(None, None, None))
except RuntimeError as exc:
    print('twice', str(exc))
