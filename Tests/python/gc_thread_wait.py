import _thread
import gc


gate = _thread.allocate_lock()
ready = _thread.allocate_lock()
done = _thread.allocate_lock()
events = []

gate.acquire()
ready.acquire()
done.acquire()


def worker():
    events.append('ready')
    ready.release()
    gate.acquire()
    gate.release()
    events.append('done')
    done.release()


_thread.start_new_thread(worker, ())
ready.acquire()
print(events)
print(type(gc.collect()))
gate.release()
done.acquire()
print(events)
