import _thread
import gc

# CPython's most recent _set_sentinel owns the thread-state exit notification.
ready = _thread.allocate_lock()
gate = _thread.allocate_lock()
ready.acquire()
gate.acquire()
locks = []
events = []
old = _thread.stack_size()
print('default stack', old)
print('set stack', _thread.stack_size(1048576))

def worker():
    first = _thread._set_sentinel()
    first.acquire()
    last = _thread._set_sentinel()
    last.acquire()
    locks.extend((first, last))
    ready.release()
    with gate:
        gc.collect()
        events.append('finished')

_thread.start_new_thread(worker, ())
assert ready.acquire(timeout=5)
first, last = locks
print('running', first.locked(), last.locked())
gc.collect()
gate.release()
assert last.acquire(timeout=5)
print('retired', events, first.locked())
last.release()
first.release()

print('query stack', _thread.stack_size())
print('restore stack', _thread.stack_size(old))
for value in (-1, 1, 1.5, 1 << 100):
    try:
        _thread.stack_size(value)
    except (TypeError, ValueError, OverflowError) as error:
        print('invalid stack', type(error).__name__)
    else:
        raise AssertionError('expected stack size rejection')
