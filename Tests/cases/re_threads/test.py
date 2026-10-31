import gc
import re
import _thread

p = re.compile(r'(?P<word>[a-z]+)|(\d+)')
results = [None] * 4
done = [_thread.allocate_lock() for _ in results]
for lock in done:
    lock.acquire()

def worker(index):
    try:
        scanner = p.scanner('abc 12 def 34')
        found = []
        while True:
            match = scanner.search()
            if match is None:
                break
            gc.collect()
            found.append((match.group(), match.lastgroup, match.span()))
        results[index] = found
    except BaseException as exc:
        results[index] = type(exc).__name__
    finally:
        done[index].release()

for index in range(len(results)):
    _thread.start_new_thread(worker, (index,))
for lock in done:
    lock.acquire()
print(all(result == results[0] for result in results))
print(results[0])
