import _thread


done = _thread.allocate_lock()
done.acquire()
results = []


def worker(limit):
    total = 0
    for i in range(limit):
        total += i
        total += len(str(i))
        total += int(float(i))
    results.append(total)
    done.release()


_thread.start_new_thread(worker, (1000,))
done.acquire()
done.release()
print(len(results))
print(results[0] > 0)
