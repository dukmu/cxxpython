import _thread
import atexit
import gc

atexit._clear()
entered = _thread.allocate_lock()
release = _thread.allocate_lock()
started = _thread.allocate_lock()
done = [_thread.allocate_lock(), _thread.allocate_lock()]
for lock in (entered, release, started, done[0], done[1]):
    lock.acquire()

events = []
errors = []

def older():
    events.append('older')

def newer():
    events.append('newer')
    if events.count('newer') == 1:
        entered.release()
        release.acquire()
    gc.collect()

def run(index):
    try:
        if index == 1:
            started.release()
        atexit._run_exitfuncs()
    except BaseException as error:
        errors.append(type(error).__name__)
    finally:
        done[index].release()

atexit.register(older)
atexit.register(newer)
_thread.start_new_thread(run, (0,))
entered.acquire()
_thread.start_new_thread(run, (1,))
started.acquire()
for i in range(10):
    gc.collect()
release.release()
for lock in done:
    lock.acquire()
print(errors, events, atexit._ncallbacks())
