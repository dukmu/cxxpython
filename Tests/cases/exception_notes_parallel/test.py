import _thread
import gc

error = ValueError('shared')
failures = []
gates = [_thread.allocate_lock(), _thread.allocate_lock()]
for gate in gates:
    gate.acquire()
def worker(index):
    try:
        for number in range(40):
            error.add_note(str(index) + ':' + str(number))
            gc.collect()
    except BaseException as failure:
        failures.append(type(failure).__name__)
    finally:
        gates[index].release()
for index in range(2):
    _thread.start_new_thread(worker, (index,))
for gate in gates:
    gate.acquire()
print(failures, len(error.__notes__), len(set(error.__notes__)))
