import _thread
import gc

def target():
    pass

errors = []
gates = [_thread.allocate_lock(), _thread.allocate_lock()]
for gate in gates:
    gate.acquire()

def worker(index):
    try:
        for n in range(100):
            if (index + n) % 2:
                target.__annotations__ = {"n": n}
            else:
                target.__annotations__ = None
            assert isinstance(target.__annotations__, dict)
            if n % 11 == 0:
                gc.collect()
    except BaseException as error:
        errors.append(type(error).__name__)
    finally:
        gates[index].release()

for i in range(2):
    _thread.start_new_thread(worker, (i,))
for gate in gates:
    gate.acquire()
print(errors)
print(isinstance(target.__annotations__, dict))
