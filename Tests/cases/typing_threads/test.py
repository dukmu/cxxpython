import _thread
import gc
import typing

T = typing.TypeVar('T')

class Box(typing.Generic[T]):
    def __init__(self, value: T):
        self.value = value

def annotated(arg: 'Box[int]') -> 'list[str]':
    return []

done = [_thread.allocate_lock() for _ in range(4)]
for lock in done:
    lock.acquire()
results = [False] * 4

def worker(index):
    try:
        for turn in range(4):
            alias = Box[int]
            value = alias(index)
            gc.collect()
            assert value.value == index
            assert value.__orig_class__ == alias
            assert typing.get_origin(alias) is Box
            assert typing.get_args(alias) == (int,)
            assert typing.get_type_hints(annotated) == {'arg': alias, 'return': list[str]}
        results[index] = True
    except BaseException as exc:
        results[index] = type(exc).__name__
    finally:
        done[index].release()

for index in range(4):
    _thread.start_new_thread(worker, (index,))
for lock in done:
    lock.acquire()
print(results)
