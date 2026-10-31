import _thread
import gc
import time

ready = _thread.allocate_lock()
done = _thread.allocate_lock()
ready.acquire()
done.acquire()
progress = [False]

def worker():
    ready.release()
    time.sleep(0.2)
    print("progress while sleeping", progress[0])
    done.release()

_thread.start_new_thread(worker, ())
ready.acquire()
time.sleep(0.02)
gc.collect()
progress[0] = True
done.acquire()
