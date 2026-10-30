import _thread
import abc
import gc

entered = _thread.allocate_lock()
resume = _thread.allocate_lock()
done = _thread.allocate_lock()
entered.acquire()
resume.acquire()
done.acquire()
first = [True]
results = [None, None]

class Root(abc.ABC):
    @classmethod
    def __subclasshook__(cls, sub):
        gc.collect()
        if sub is Probe and first[0]:
            first[0] = False
            entered.release()
            resume.acquire()
            resume.release()
            return False
        return NotImplemented

class Virtual:
    pass

class Probe(Virtual):
    pass

print(issubclass(Virtual, Root))
token = abc.get_cache_token()

def worker():
    results[0] = issubclass(Probe, Root)
    results[1] = issubclass(Probe, Root)
    done.release()

_thread.start_new_thread(worker, ())
entered.acquire()
Root.register(Virtual)
resume.release()
done.acquire()
print(abc.get_cache_token() > token)
print(results)
print(issubclass(Virtual, Root))
