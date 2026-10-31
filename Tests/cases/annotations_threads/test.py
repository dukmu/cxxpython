import _thread
import gc

entered = _thread.allocate_lock()
release = _thread.allocate_lock()
finished = _thread.allocate_lock()
entered.acquire()
release.acquire()
finished.acquire()
events = []
started = [False]

def annotation():
    try:
        gc.collect()
        started[0] = True
        events.append('entered')
        entered.release()
        release.acquire()
        gc.collect()
        try:
            raise ValueError('callback')
        except ValueError:
            events.append('caught')
        return int
    finally:
        events.append('finally')

def worker():
    try:
        namespace = {'annotation': annotation}
        exec('value: annotation() = 42', namespace)
        events.append((namespace['value'], namespace['__annotations__']['value'] is int))
    except BaseException as error:
        events.append(('failed', type(error).__name__))
        if not started[0]:
            entered.release()
    finally:
        finished.release()

_thread.start_new_thread(worker, ())
entered.acquire()
print(events)
release.release()
finished.acquire()
print(events)
