import _thread
import gc

finished = [_thread.allocate_lock(), _thread.allocate_lock()]
for lock in finished:
    lock.acquire()
results = [None, None]
errors = []

def worker(index):
    try:
        state = [0]
        def produce():
            state[0] += 1
            gc.collect()
            return state[0]
        results[index] = list(iter(produce, 4))
    except BaseException as exc:
        errors.append(type(exc).__name__)
    finally:
        finished[index].release()

for index in range(2):
    _thread.start_new_thread(worker, (index,))
for lock in finished:
    lock.acquire()
print(errors, results)
