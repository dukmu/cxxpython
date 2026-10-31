import gc
import random
import threading

system = random.SystemRandom()
print('system', 0 <= system.random() < 1, 0 <= system.getrandbits(65) < (1 << 65),
      len(system.randbytes(32)) == 32)
print('seed', system.seed(42) is None)
for method in (system.getstate, system.setstate):
    try:
        method() if method == system.getstate else method(None)
    except NotImplementedError:
        print('no state', True)
    else:
        raise AssertionError('SystemRandom state')

failures = []
results = [None] * 4


def worker(index):
    try:
        generator = random.Random(100 + index)
        values = []
        for step in range(20):
            values.append(generator.randrange(1000))
            assert 0 <= system.randrange(1000) < 1000
            gc.collect()
        results[index] = values
    except BaseException as exc:
        failures.append((index, type(exc).__name__, str(exc)))


threads = [threading.Thread(target=worker, args=(i,), daemon=True) for i in range(4)]
for thread in threads:
    thread.start()
for thread in threads:
    thread.join(30)
assert not any(thread.is_alive() for thread in threads), 'worker timed out'
assert not failures, failures
for index in range(4):
    expected = random.Random(100 + index)
    assert results[index] == [expected.randrange(1000) for step in range(20)]
print('threads', [len(result) for result in results], failures)
