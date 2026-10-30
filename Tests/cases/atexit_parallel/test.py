import _thread
import atexit
import gc

atexit._clear()
gates = [_thread.allocate_lock(), _thread.allocate_lock()]
errors = []
values = []
for gate in gates:
    gate.acquire()

def save(value):
    values.append(value)

def worker(index):
    try:
        for i in range(50):
            atexit.register(save, index)
            if i % 7 == 0:
                gc.collect()
    except BaseException as error:
        errors.append(type(error).__name__)
    finally:
        gates[index].release()

_thread.start_new_thread(worker, (0,))
_thread.start_new_thread(worker, (1,))
for gate in gates:
    gate.acquire()
print(errors, atexit._ncallbacks())
gc.collect()
atexit._run_exitfuncs()
print(len(values), values.count(0), values.count(1), atexit._ncallbacks())
