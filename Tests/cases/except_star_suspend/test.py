import _thread
import gc
import sys


def describe(exc):
    if isinstance(exc, BaseExceptionGroup):
        return (type(exc).__name__, exc.message, [describe(e) for e in exc.exceptions])
    return (type(exc).__name__, str(exc))


def suspended():
    try:
        raise ExceptionGroup("g", [ValueError("v"), TypeError("t")])
    except* ValueError as part:
        yield ("yield", describe(part), sys.exception() is part)
        print("resumed", sys.exception() is part)
    except* TypeError as part:
        print("later", describe(part))


def resume_generator(iterator, done, errors):
    try:
        iterator.send(None)
    except StopIteration:
        pass
    except BaseException as exc:
        errors.append(describe(exc))
    finally:
        done.release()


gen = suspended()
print(next(gen))
done = _thread.allocate_lock()
done.acquire()
errors = []
_thread.start_new_thread(resume_generator, (gen, done, errors))
done.acquire()
assert not errors, errors
print("caller exception", sys.exception())

for action in ("close", "throw"):
    gen = suspended()
    print(action, next(gen))
    try:
        if action == "close":
            gen.close()
        else:
            gen.throw(RuntimeError("injected"))
    except BaseException as exc:
        print(action, "result", describe(exc))


class Pause:
    def __await__(self):
        yield "pause"
        return "ready"


async def suspended_async():
    try:
        raise ExceptionGroup("async", [ValueError("v")])
    except* ValueError as part:
        print("await before", sys.exception() is part)
        value = await Pause()
        print("await after", value, sys.exception() is part)


def resume_async(coro, done, errors):
    try:
        coro.send(None)
    except StopIteration:
        print("await finished")
    except BaseException as exc:
        errors.append(describe(exc))
    finally:
        done.release()


coro = suspended_async()
print("await yielded", coro.send(None))
done = _thread.allocate_lock()
done.acquire()
errors = []
_thread.start_new_thread(resume_async, (coro, done, errors))
done.acquire()
assert not errors, errors

shared = ExceptionGroup("shared", [ValueError("v"), TypeError("t")])
start = _thread.allocate_lock()
start.acquire()
ready = [_thread.allocate_lock(), _thread.allocate_lock()]
done = [_thread.allocate_lock(), _thread.allocate_lock()]
for lock in ready + done:
    lock.acquire()
results = [None, None]
errors = [None, None]


def worker(index):
    try:
        ready[index].release()
        start.acquire()
        start.release()
        try:
            try:
                raise shared
            except* ValueError as part:
                gc.collect()
                results[index] = (describe(part), sys.exception() is part)
        except* TypeError:
            pass
    except BaseException as exc:
        errors[index] = describe(exc)
    finally:
        done[index].release()


for index in range(2):
    _thread.start_new_thread(worker, (index,))
for lock in ready:
    lock.acquire()
start.release()
for lock in done:
    lock.acquire()
assert errors == [None, None], errors
print("shared", results, describe(shared))
