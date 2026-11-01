import _thread

ready = _thread.allocate_lock()
gate = _thread.allocate_lock()
done = _thread.allocate_lock()
ready.acquire()
gate.acquire()
done.acquire()


def generator():
    value = 42
    ready.release()
    gate.acquire()
    yield value


gen = generator()
frame = gen.gi_frame


def worker():
    try:
        next(gen)
    finally:
        done.release()


_thread.start_new_thread(worker, ())
ready.acquire()
try:
    frame.f_locals
except RuntimeError:
    pass
print("foreign read completed")
try:
    next(gen)
except ValueError:
    print("resume rejected")
try:
    frame.clear()
except RuntimeError:
    print("clear rejected")
gate.release()
done.acquire()
print("suspended", frame.f_locals["value"])
frame.clear()
print("cleared", gen.gi_frame is None)
