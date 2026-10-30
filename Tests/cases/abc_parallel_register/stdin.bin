import _thread
import abc
import gc
import _abc

class Root(abc.ABC):
    @classmethod
    def __subclasshook__(cls, sub):
        gc.collect()
        return NotImplemented

class One:
    pass

class Two:
    pass

classes = [One, Two]
results = [False, False]
gate = _thread.allocate_lock()
gate.acquire()
ready = [_thread.allocate_lock(), _thread.allocate_lock()]
done = [_thread.allocate_lock(), _thread.allocate_lock()]
for lock in ready + done:
    lock.acquire()

print(issubclass(One, Root), issubclass(Two, Root))
token = abc.get_cache_token()

def worker(index):
    ready[index].release()
    gate.acquire()
    gate.release()
    cls = classes[index]
    Root.register(cls)
    gc.collect()
    results[index] = issubclass(cls, Root) and isinstance(cls(), Root)
    done[index].release()

_thread.start_new_thread(worker, (0,))
_thread.start_new_thread(worker, (1,))
for lock in ready:
    lock.acquire()
gate.release()
for lock in done:
    lock.acquire()
print(results)
print(abc.get_cache_token() > token)
print(len(_abc._get_dump(Root)[0]))
