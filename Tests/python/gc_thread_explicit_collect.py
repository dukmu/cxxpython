import gc
import threading


results = [0, 0, 0, 0]


def worker(tid):
    total = 0
    for i in range(200):
        total = total + i + tid
        if i % 25 == 0:
            gc.collect()
    results[tid] = total


threads = []
for tid in range(4):
    thread = threading.Thread(target=worker, args=(tid,))
    threads.append(thread)
    thread.start()

for thread in threads:
    thread.join()

print(len(results))
print(sum(results) > 0)
