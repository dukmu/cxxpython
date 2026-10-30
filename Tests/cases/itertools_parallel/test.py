import _thread
import gc
from itertools import chain, combinations, tee

def consume_shared(inputs):
    results = [[], []]
    errors = []
    gates = [_thread.allocate_lock(), _thread.allocate_lock()]
    for gate in gates:
        gate.acquire()
    def worker(index):
        try:
            while True:
                try:
                    value = next(inputs[index])
                except StopIteration:
                    break
                except RuntimeError:
                    gc.collect()
                    continue
                results[index].append(value)
                gc.collect()
        except BaseException as error:
            errors.append(type(error).__name__)
        finally:
            gates[index].release()
    for index in range(2):
        _thread.start_new_thread(worker, (index,))
    for gate in gates:
        gate.acquire()
    assert not errors, errors
    return results

source = chain(range(60), range(60, 90))
first, second = consume_shared((source, source))
print(sorted(first + second) == list(range(90)))
source = combinations(range(8), 2)
first, second = consume_shared((source, source))
print(sorted(first + second) == list(combinations(range(8), 2)))
first, second = consume_shared(tee(range(70)))
print(first == list(range(70)), second == first)
