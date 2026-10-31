import gc
import os
import posix
import threading

count = os.cpu_count()
print('logical count', count is None or type(count) is int and count > 0)
print('exports', os.cpu_count is posix.cpu_count, 'cpu_count' in os.__all__)
for args, kwargs in (((1,), {}), ((), {'unused': True})):
    try:
        os.cpu_count(*args, **kwargs)
    except TypeError:
        print('no arguments')

errors = []
def worker():
    try:
        for iteration in range(30):
            current = os.cpu_count()
            assert current is None or type(current) is int and current > 0
            gc.collect()
    except BaseException as error:
        errors.append(type(error).__name__)

threads = [threading.Thread(target=worker) for unused in range(2)]
for thread in threads:
    thread.start()
for thread in threads:
    thread.join(5)
assert all(not thread.is_alive() for thread in threads)
print('parallel query', not errors)
