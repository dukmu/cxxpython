from _typing import TypeVar, ParamSpec, TypeVarTuple
import _thread
import _weakref
import gc


ready = _thread.allocate_lock()
ready.acquire()
done = [_thread.allocate_lock(), _thread.allocate_lock()]
for lock in done:
    lock.acquire()
shared = []
refs = []
errors = []
exited = [None, None]


def producer():
    try:
        exited[0] = _thread._set_sentinel()
        exited[0].acquire()
        values = [TypeVar('T'), ParamSpec('P'), TypeVarTuple('Ts')]
        for value in values:
            value.owner = 'producer'
            refs.append(_weakref.ref(value))
        shared.extend(values)
        gc.collect()
    except BaseException as error:
        errors.append(('producer', type(error).__name__))
    finally:
        ready.release()
        done[0].release()


def consumer():
    try:
        exited[1] = _thread._set_sentinel()
        exited[1].acquire()
        ready.acquire()
        if len(shared) == 3:
            for value in shared:
                assert value.owner == 'producer'
                value.owner = 'consumer'
                value.seen = True
                assert _weakref.ref(value)() is value
                gc.collect()
        else:
            errors.append(('consumer', 'missing values'))
    except BaseException as error:
        errors.append(('consumer', type(error).__name__))
    finally:
        done[1].release()


_thread.start_new_thread(producer, ())
_thread.start_new_thread(consumer, ())
for lock in done:
    lock.acquire()
# Work acknowledgement is not thread exit; wait for thread-state/root retirement.
for sentinel in exited:
    sentinel.acquire()
print('errors', errors)
print('handoff', [(value.__name__, value.owner, value.seen) for value in shared])
print('alive', [ref() is value for ref, value in zip(refs, shared)])
shared.clear()
gc.collect()
print('collected', [ref() is None for ref in refs])
