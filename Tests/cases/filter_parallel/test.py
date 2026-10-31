import _thread
import gc


class Item:
    def __init__(self, value):
        self.value = value


class Source:
    def __init__(self):
        self.index = 0
        self.lock = _thread.allocate_lock()

    def __iter__(self):
        return self

    def __next__(self):
        with self.lock:
            if self.index == 40:
                raise StopIteration
            value = self.index
            self.index += 1
        gc.collect()
        return Item(value)


class Flag:
    def __init__(self, value):
        self.value = value

    def __bool__(self):
        gc.collect()
        return self.value % 2 == 1


class Predicate:
    def __init__(self):
        self.ready = [_thread.allocate_lock(), _thread.allocate_lock()]
        for lock in self.ready:
            lock.acquire()

    def __call__(self, item):
        # Both callbacks must enter before either finishes. A parent filter lock
        # across user code would deadlock this rendezvous.
        if item.value < 2:
            self.ready[item.value].release()
            other = self.ready[1 - item.value]
            other.acquire()
            other.release()
        gc.collect()
        return Flag(item.value)


def consume(inputs):
    results = [[], []]
    errors = []
    done = [_thread.allocate_lock(), _thread.allocate_lock()]
    for lock in done:
        lock.acquire()

    def worker(index):
        try:
            for item in inputs[index]:
                results[index].append(item.value)
                gc.collect()
        except BaseException as exc:
            errors.append(type(exc).__name__)
        finally:
            done[index].release()

    for index in range(2):
        _thread.start_new_thread(worker, (index,))
    for lock in done:
        lock.acquire()
    assert not errors, errors
    return sorted(results[0] + results[1])


source, predicate = Source(), Predicate()
shared = filter(predicate, source)
print('shared filter', consume((shared, shared)) == list(range(1, 40, 2)))
source, predicate = Source(), Predicate()
print('shared child', consume((filter(predicate, source), filter(predicate, source))) == list(range(1, 40, 2)))
