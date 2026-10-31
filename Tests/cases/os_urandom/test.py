import gc
import os
import threading


class Size:
    def __index__(self):
        gc.collect()
        return 32


print('empty', os.urandom(0) == b'')
data = os.urandom(Size())
print('bytes', type(data) is bytes, len(data))
for value in (-1, 1.5, 1 << 100):
    try:
        os.urandom(value)
    except (ValueError, TypeError, OverflowError) as exc:
        print('error', type(exc).__name__)
    else:
        raise AssertionError(value)

failures = []
results = [False] * 4


def worker(index):
    try:
        for i in range(20):
            value = os.urandom(257)
            assert type(value) is bytes and len(value) == 257
            gc.collect()
        results[index] = True
    except BaseException as exc:
        failures.append((index, type(exc).__name__, str(exc)))


threads = [threading.Thread(target=worker, args=(i,), daemon=True) for i in range(4)]
for thread in threads:
    thread.start()
for thread in threads:
    thread.join(30)
assert not any(thread.is_alive() for thread in threads), 'worker timed out'
assert not failures, failures
print('threads', results, failures)
