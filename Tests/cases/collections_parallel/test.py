import _thread
import gc
from collections import deque, Counter
from functools import lru_cache

queue = deque()
errors = []
gates = [_thread.allocate_lock(), _thread.allocate_lock()]
for gate in gates:
    gate.acquire()
@lru_cache(maxsize=16)
def square(value):
    gc.collect()
    return value * value
def worker(index):
    try:
        for number in range(80):
            queue.append((index, number))
            assert square(number % 9) == (number % 9) ** 2
            if number % 11 == 0:
                gc.collect()
    except BaseException as error:
        errors.append(type(error).__name__)
    finally:
        gates[index].release()
for index in range(2):
    _thread.start_new_thread(worker, (index,))
for gate in gates:
    gate.acquire()
print(errors)
counts = Counter()
while queue:
    index, number = queue.popleft()
    counts[index] += 1
print(sorted(counts.items()), len(queue))
print(square.cache_info().currsize <= 16)
