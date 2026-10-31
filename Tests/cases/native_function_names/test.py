import gc
import os
import threading
import time

functions = (len, iter, next, print, os.cpu_count, time.gmtime, time.localtime, time.strftime)
print('names', [function.__name__ for function in functions])
for operation in ('set', 'delete'):
    try:
        if operation == 'set':
            len.__name__ = 'changed'
        else:
            del len.__name__
    except AttributeError:
        print('readonly', operation)

errors = []
def worker():
    try:
        for iteration in range(10):
            for function in functions:
                assert type(function.__name__) is str
            gc.collect()
    except BaseException as error:
        errors.append(type(error).__name__)

threads = [threading.Thread(target=worker) for unused in range(2)]
for thread in threads:
    thread.start()
for thread in threads:
    thread.join(5)
assert all(not thread.is_alive() for thread in threads)
print('parallel names', not errors)
