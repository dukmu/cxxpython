import gc
import _thread

gc.disable()
shared = [[] for _ in range(4)]
gates = [_thread.allocate_lock() for _ in shared]
errors = []
for gate in gates:
    gate.acquire()

def worker(index):
    try:
        for i in range(25):
            value = [index, i]
            shared[index][:] = [value, shared]
            refs = gc.get_referents(shared[index])
            assert any(item is shared for item in refs)
            if i % 5 == 0:
                gc.collect()
        objects = gc.get_objects()
        assert any(item is value for item in objects)
        del objects
    except BaseException as error:
        errors.append(type(error).__name__)
    finally:
        gates[index].release()

for i in range(len(shared)):
    _thread.start_new_thread(worker, (i,))
for gate in gates:
    gate.acquire()
print(errors)
print([row[0][1] for row in shared])
gc.enable()
