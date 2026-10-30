import _thread
import gc

def f(x=1, *, y=3):
    return x, y

errors = []
gates = [_thread.allocate_lock(), _thread.allocate_lock()]
for gate in gates:
    gate.acquire()
def worker(index):
    try:
        for n in range(40):
            f.__defaults__ = (index + 1,)
            f.__kwdefaults__ = {'y': index + 3}
            f.__name__ = 'worker' + str(index)
            gc.collect()
            x, y = f()
            assert x in (1, 2) and y in (3, 4)
    except BaseException as error:
        errors.append(type(error).__name__)
    finally:
        gates[index].release()
for index in range(2):
    _thread.start_new_thread(worker, (index,))
for gate in gates:
    gate.acquire()
print(errors)
print(f.__name__ in ('worker0', 'worker1'))
