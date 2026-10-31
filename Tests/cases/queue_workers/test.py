import queue
import threading

# Derived from CPython 3.12.14 test_queue.queue_join_test: two consumers,
# task accounting, a second join, sentinel shutdown, and explicit thread joins.
q = queue.Queue(4)
values = []
errors = []
lock = threading.Lock()


def worker():
    while True:
        item = q.get()
        try:
            if item is None:
                return
            with lock:
                values.append(item)
        except BaseException as exc:
            errors.append(type(exc).__name__)
        finally:
            q.task_done()


threads = [threading.Thread(target=worker) for _ in range(2)]
for t in threads:
    t.start()
for item in range(20):
    q.put(item)
q.join()
print("completed", sorted(values) == list(range(20)), sum(values))
for t in threads:
    q.put(None)
q.join()
for t in threads:
    t.join(5)
assert all(not t.is_alive() for t in threads) and not errors
print("workers stopped", len(threads))
