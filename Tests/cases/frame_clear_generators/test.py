import _thread
import gc
import weakref


class Item:
    pass


events = []


def tracked(item):
    try:
        yield 'paused'
    finally:
        events.append('finally')


item = Item()
ref = weakref.ref(item, lambda ref: events.append('collected'))
gen = tracked(item)
frame = gen.gi_frame
del item
print('created retained', ref() is not None)
print('created clear', frame.clear())
gc.collect()
print('created closed', gen.gi_frame is None)
print('created repeat', frame.clear())
del frame, gen
gc.collect()
print('created collected', ref() is None, events)

events = []
item = Item()
ref = weakref.ref(item, lambda ref: events.append('collected'))
gen = tracked(item)
frame = gen.gi_frame
del item
print('suspended', next(gen))
print('suspended clear', frame.clear())
gc.collect()
print('suspended closed', 'finally' in events, gen.gi_frame is None)
print('suspended repeat', frame.clear())
del frame, gen
gc.collect()
print('suspended collected', ref() is None, events)

started = _thread.allocate_lock()
started.acquire()
resume = _thread.allocate_lock()
resume.acquire()
done = _thread.allocate_lock()
done.acquire()
errors = []


def running():
    started.release()
    resume.acquire()
    yield 'done'


gen = running()
frame = gen.gi_frame


def worker():
    try:
        next(gen)
    except BaseException as exc:
        errors.append((type(exc).__name__, str(exc)))
    finally:
        done.release()


_thread.start_new_thread(worker, ())
started.acquire()
try:
    frame.clear()
except RuntimeError as exc:
    print('running', type(exc).__name__, str(exc))
finally:
    resume.release()
done.acquire()
print('worker errors', errors)
frame.clear()
print('running cleanup', gen.gi_frame is None)


class Pause:
    def __await__(self):
        yield 'paused'


async def coroutine():
    try:
        await Pause()
    finally:
        events.append('coroutine finally')


events = []
coro = coroutine()
frame = coro.cr_frame
print('coroutine suspended', coro.send(None))
print('coroutine clear', frame.clear())
print('coroutine closed', events, coro.cr_frame is None)
