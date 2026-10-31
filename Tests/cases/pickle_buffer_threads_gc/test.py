import gc
import pickle
import threading

source = bytearray(b'parallel buffer')
parent = memoryview(source)
failures = []
results = [0] * 3


def worker(index):
    try:
        for _ in range(20):
            buffer = pickle.PickleBuffer(parent)
            raw = buffer.raw()
            assert raw.obj is parent and raw.tobytes() == source
            gc.collect()
            buffer.release()
            raw.release()
            results[index] += 1
    except BaseException as error:
        failures.append((type(error).__name__, str(error)))


threads = [threading.Thread(target=worker, args=(index,)) for index in range(3)]
for thread in threads:
    thread.start()
for thread in threads:
    thread.join()
assert not failures, failures
assert results == [20, 20, 20]
parent.release()
source.extend(b'!')
print('shared exporter', results, source)
