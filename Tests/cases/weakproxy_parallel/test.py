import _weakref as weakref
import _thread
import gc

class Target:
    token = 17
target = Target()
canonical = weakref.proxy(target)
errors = []
gates = [_thread.allocate_lock(), _thread.allocate_lock()]
for gate in gates:
    gate.acquire()

def worker(index):
    try:
        for n in range(50):
            p = weakref.proxy(target)
            assert p is canonical
            gc.collect()
            assert p.token == 17
    except BaseException as error:
        errors.append(type(error).__name__)
    finally:
        gates[index].release()

for i in range(2):
    _thread.start_new_thread(worker, (i,))
for gate in gates:
    gate.acquire()
print(errors)
print(weakref.getweakrefcount(target))
