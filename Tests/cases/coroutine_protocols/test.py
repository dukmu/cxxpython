class Bad:
    def __await__(self):
        return []

async def invalid(value):
    return await value

for value in (1, Bad()):
    c = invalid(value)
    try:
        c.send(None)
    except TypeError:
        print('bad awaitable', c.cr_frame is None)

events = []
class Probe:
    def __await__(self):
        try:
            yield 'pause'
        finally:
            events.append('closed')

async def f():
    try:
        await Probe()
    finally:
        events.append('outer')

c = f()
print(c.send(None))
print(c.close(), events)

async def escapes():
    raise StopIteration('bad')
c = escapes()
try:
    c.send(None)
except RuntimeError as e:
    print(str(e), type(e.__cause__).__name__)

async def reenter():
    try:
        c.send(None)
    except ValueError as e:
        return str(e)
c = reenter()
try:
    c.send(None)
except StopIteration as e:
    print(e.value)
