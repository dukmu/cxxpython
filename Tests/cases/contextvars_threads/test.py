import _thread
from contextvars import Context, ContextVar, copy_context

var = ContextVar('var', default='default')
var.set('main')
shared = Context()
entered = _thread.allocate_lock()
release = _thread.allocate_lock()
done = _thread.allocate_lock()
entered.acquire()
release.acquire()
done.acquire()
result = []


def inside():
    var.set('shared')
    entered.release()
    release.acquire()


def worker():
    try:
        result.append(var.get())
        result.append(len(copy_context()))
        shared.run(inside)
        result.append(var.get())
    except BaseException as error:
        result.append(type(error).__name__)
        if entered.locked():
            entered.release()
    finally:
        done.release()


_thread.start_new_thread(worker, ())
entered.acquire()
try:
    shared.run(lambda: None)
except RuntimeError:
    result.append('already entered')
release.release()
done.acquire()
print(result)
print(var.get(), shared[var])
print(shared.run(var.get))
