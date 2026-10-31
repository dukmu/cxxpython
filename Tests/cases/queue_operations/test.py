import queue

# Derived from CPython 3.12.14 test_queue basic, nowait, negative-timeout,
# and task_done tests, with each ordering checked explicitly.
for name, cls in (("Queue", queue.Queue), ("LifoQueue", queue.LifoQueue),
                  ("PriorityQueue", queue.PriorityQueue), ("SimpleQueue", queue.SimpleQueue)):
    q = cls()
    for value in (3, 1, 2):
        q.put(value)
    print(name, [q.get_nowait() for _ in range(3)], q.empty())
    try:
        q.get_nowait()
    except queue.Empty:
        print("empty")

q = queue.Queue(1)
q.put_nowait(1)
try:
    q.put_nowait(2)
except queue.Full:
    print("full")
try:
    q.put(2, timeout=-1)
except ValueError:
    print("negative put timeout")
print("get", q.get())
q.task_done()
q.join()
try:
    q.task_done()
except ValueError:
    print("extra task_done")
try:
    q.get(timeout=-1)
except ValueError:
    print("negative get timeout")
