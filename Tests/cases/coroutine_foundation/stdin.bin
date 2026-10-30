events = []

class Pause:
    def __await__(self):
        events.append('pause')
        value = yield 'ready'
        return value + 1

async def child():
    events.append('child')
    return await Pause()

async def parent():
    events.append('parent')
    try:
        value = await child()
        return value * 2
    finally:
        events.append('finally')

c = parent()
print(type(c).__name__, c.cr_running, c.cr_suspended, events)
try:
    iter(c)
except TypeError:
    print('not iterable')
w = c.__await__()
print(type(w).__name__, iter(w) is w, next(w))
print(c.cr_suspended, type(c.cr_await).__name__, c.cr_await.cr_suspended)
try:
    w.send(20)
except StopIteration as e:
    print('return', e.value)
print(events, c.cr_frame, c.cr_await, c.cr_suspended)
for action in (lambda: c.send(None), lambda: next(w), lambda: c.throw(ValueError())):
    try:
        action()
    except RuntimeError as e:
        print(type(e).__name__, str(e))
print(c.close())

async def recover():
    try:
        await Pause()
    except LookupError as e:
        return e.args[0]

c = recover()
print(c.send(None))
try:
    c.throw(LookupError('caught'))
except StopIteration as e:
    print(e.value)

c = parent()
print(c.send(None))
print(c.close(), events[-1])

async def immediate():
    return 7

c = immediate()
try:
    c.send(1)
except TypeError as e:
    print(str(e))
try:
    c.send(None)
except StopIteration as e:
    print(e.value)

for source in ('await x', 'def f():\n await x', 'class C:\n await x'):
    try:
        compile(source, '<scope>', 'exec')
    except SyntaxError:
        print('scope rejected')
