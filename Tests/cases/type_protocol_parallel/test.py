import _thread
import gc
import types

class Parameter:
    def __typing_subst__(self, value):
        gc.collect()
        return value

T = Parameter()
alias = list[T] | dict[str, T]
expected = list[int] | dict[str, int]
shared = types.SimpleNamespace()
errors = []
done = [_thread.allocate_lock(), _thread.allocate_lock()]
for gate in done:
    gate.acquire()

def worker(index):
    try:
        for n in range(30):
            assert alias.__parameters__ == (T,)
            assert alias[int] == expected
            assert hash(alias[int]) == hash(expected)
            shared.last = (index, n)
            gc.collect()
            assert isinstance(shared.last, tuple)
    except BaseException as error:
        errors.append(type(error).__name__)
    finally:
        done[index].release()

for index in range(2):
    _thread.start_new_thread(worker, (index,))
for gate in done:
    gate.acquire()
print(errors)
print('parallel type protocols ok')
