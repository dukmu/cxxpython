import _thread
import gc

lock = _thread.RLock()
ready = _thread.allocate_lock()
done = _thread.allocate_lock()
ready.acquire()
done.acquire()
lock.acquire()
lock.acquire()
results = []

def worker():
    results.append((lock._is_owned(), lock._recursion_count(), lock.acquire(False)))
    try:
        lock.release()
    except RuntimeError:
        results.append('not owner')
    results.append(lock.acquire(timeout=0.01))
    ready.release()
    lock.acquire()
    results.append((lock._is_owned(), lock._recursion_count()))
    for i in range(10):
        gc.collect()
    lock.release()
    done.release()

_thread.start_new_thread(worker, ())
ready.acquire()
for i in range(10):
    gc.collect()
print(results, lock._recursion_count())
lock.release()
lock.release()
done.acquire()
print(results, lock._is_owned())
