import sys
import gc
import _thread

gate = _thread.allocate_lock()
gate.acquire()
done = [_thread.allocate_lock(), _thread.allocate_lock()]
for lock in done:
    lock.acquire()
results = [None, None]
def worker(i):
    gate.acquire()
    gate.release()
    for j in range(10):
        text = ''.join(['parallel ', 'intern ', 'value'])
        value = sys.intern(text)
        gc.collect()
    results[i] = value
    done[i].release()
for i in range(2):
    _thread.start_new_thread(worker, (i,))
gate.release()
for lock in done:
    lock.acquire()
print(results[0] is results[1], results[0] == 'parallel intern value')
