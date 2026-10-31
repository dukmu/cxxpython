import _thread
import gc

events = []
errors = []
results = []
ready = _thread.allocate_lock()
ready.acquire()
done = _thread.allocate_lock()
done.acquire()

def evaluate():
    events.append('called')
    gc.collect()
    return 123

type Shared = evaluate()

def worker():
    try:
        ready.acquire()
        results.append(Shared.__value__)
        gc.collect()
        results.append(Shared.__value__)
    except BaseException as error:
        errors.append(type(error).__name__)
    finally:
        done.release()

_thread.start_new_thread(worker, ())
ready.release()
done.acquire()
print(errors, results, events, Shared.__value__)

transferred = []
finished = _thread.allocate_lock()
finished.acquire()

def producer():
    try:
        captured = 'from worker'
        type Transferred = captured
        transferred.append(Transferred)
        gc.collect()
    except BaseException as error:
        errors.append(type(error).__name__)
    finally:
        finished.release()

_thread.start_new_thread(producer, ())
finished.acquire()
gc.collect()
print(errors, transferred[0].__value__, transferred[0].__value__)
