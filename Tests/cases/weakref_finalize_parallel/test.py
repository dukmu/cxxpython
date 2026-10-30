import _thread
import gc
import weakref

class Item:
    pass
events = []
objects = [[], []]
errors = []
gates = [_thread.allocate_lock(), _thread.allocate_lock()]
for gate in gates:
    gate.acquire()
def callback(index, number):
    events.append((index, number))
def worker(index):
    try:
        for number in range(40):
            item = Item()
            weakref.finalize(item, callback, index, number)
            objects[index].append(item)
            del item
            gc.collect()
    except BaseException as error:
        errors.append(type(error).__name__)
    finally:
        gates[index].release()
for index in range(2):
    _thread.start_new_thread(worker, (index,))
for gate in gates:
    gate.acquire()
objects.clear()
gc.collect()
print(errors, len(events), len(set(events)))
