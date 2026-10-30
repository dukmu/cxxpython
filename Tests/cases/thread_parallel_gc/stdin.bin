import _thread
import gc

gate = _thread.allocate_lock()
gate.acquire()
ready = [_thread.allocate_lock(), _thread.allocate_lock()]
done = [_thread.allocate_lock(), _thread.allocate_lock()]
for lock in ready + done:
    lock.acquire()
results = [0, 0]
def worker(index):
    ready[index].release()
    gate.acquire()
    gate.release()
    value = 0
    for i in range(10000):
        value += i
        if i % 1000 == 0:
            gc.collect()
    results[index] = value
    done[index].release()
_thread.start_new_thread(worker, (0,))
_thread.start_new_thread(worker, (1,))
for lock in ready:
    lock.acquire()
gate.release()
for lock in done:
    lock.acquire()
print(results)
