import _thread
import gc
import math

finished = [_thread.allocate_lock() for _ in range(5)]
for lock in finished:
    lock.acquire()
results = [False] * 4
errors = []

class Operand:
    def __index__(self):
        gc.collect()
        return 10**500 + 123

def worker(i):
    try:
        for _ in range(10):
            value = Operand()
            root = math.isqrt(value)
            n = 10**500 + 123
            assert root * root <= n < (root + 1) * (root + 1)
        results[i] = True
    except BaseException as exc:
        errors.append(type(exc).__name__)
    finally:
        finished[i].release()

def collector():
    try:
        for _ in range(20):
            gc.collect()
    except BaseException as exc:
        errors.append('collector: ' + type(exc).__name__)
    finally:
        finished[4].release()

for i in range(4):
    _thread.start_new_thread(worker, (i,))
_thread.start_new_thread(collector, ())
for lock in finished:
    lock.acquire()
print(results)
print(errors)
