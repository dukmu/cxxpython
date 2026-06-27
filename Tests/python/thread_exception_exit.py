import _thread
import time

done = _thread.allocate_lock()
done.acquire()

def worker():
    done.release()
    raise TypeError('thread boom')

_thread.start_new_thread(worker, ())
done.acquire()
done.release()
time.sleep(0.05)
print('survived')
