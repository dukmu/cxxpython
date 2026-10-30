import _thread
import copy
import gc


class Probe:
    def __init__(self, value):
        self.value = value

    def __deepcopy__(self, memo):
        gc.collect()
        result = type(self)(copy.deepcopy(self.value, memo))
        memo[id(self)] = result
        return result


class Values(tuple):
    pass


shared = [[1, 2], [3]]
values = Values((shared,))
values.extra = shared
cycle = []
cycle.append(cycle)
source = [shared, shared, Probe(shared), values,
          {frozenset((1, 2))}, bytearray(b'abc'), cycle]
errors = []
completed = []
done = [_thread.allocate_lock(), _thread.allocate_lock()]
for lock in done:
    lock.acquire()


def worker(index):
    try:
        for _ in range(10):
            result = copy.deepcopy(source)
            assert result[0] is result[1]
            assert result[0] is not shared
            assert result[2].value is result[0]
            assert type(result[3]) is Values
            assert result[3][0] is result[0]
            assert result[3].extra is result[0]
            assert result[4] == source[4]
            result[5][0] = 100
            assert source[5] == bytearray(b'abc')
            assert result[6][0] is result[6]
            gc.collect()
            completed.append(index)
    except BaseException as error:
        errors.append(type(error).__name__)
    finally:
        done[index].release()


for index in range(2):
    _thread.start_new_thread(worker, (index,))
for lock in done:
    lock.acquire()
print(errors, completed.count(0), completed.count(1))
