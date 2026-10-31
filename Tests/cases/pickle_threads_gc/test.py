import gc
import pickle
import threading
import weakref


class Node:
    def __init__(self, value):
        self.value = value
        self.link = self

    def __getstate__(self):
        gc.collect()
        return self.__dict__.copy()

    def __setstate__(self, state):
        gc.collect()
        self.__dict__.update(state)


node = Node(17)
shared = [node, node]
shared.append(shared)
failures = []
results = [None] * 4


def worker(index):
    try:
        for protocol in (2, 4, 5):
            decoded = pickle.loads(pickle.dumps(shared, protocol=protocol))
            assert decoded[0] is decoded[1] and decoded[2] is decoded
            assert decoded[0].link is decoded[0] and decoded[0].value == 17
            reference = weakref.ref(decoded[0])
            del decoded
            gc.collect()
            assert reference() is None
        results[index] = True
    except BaseException as exc:
        failures.append((index, type(exc).__name__, str(exc)))


threads = [threading.Thread(target=worker, args=(i,), daemon=True) for i in range(4)]
for thread in threads:
    thread.start()
for thread in threads:
    thread.join(60)
assert not any(thread.is_alive() for thread in threads), 'worker timed out'
assert not failures, failures
print('threaded shared graphs', results)
print('worker failures', failures)
