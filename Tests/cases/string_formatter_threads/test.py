import _string
import _thread
import gc


def consume(iterator):
    buckets = [[], [], [], []]
    gates = [_thread.allocate_lock() for _ in buckets]
    failures = []

    def worker(index):
        try:
            for value in iterator:
                buckets[index].append(value)
                gc.collect()
        except BaseException as error:
            failures.append(type(error).__name__)
        finally:
            gates[index].release()

    for index, gate in enumerate(gates):
        gate.acquire()
        _thread.start_new_thread(worker, (index,))
    for gate in gates:
        gate.acquire()
    counts = {}
    for bucket in buckets:
        for value in bucket:
            key = repr(value)
            counts[key] = counts.get(key, 0) + 1
    print('values', sorted(counts.items()))
    print('failures', failures)


source = 'head{0[1].key!r:>{width}}tail' * 32
iterator = _string.formatter_parser(source)
del source
gc.collect()
consume(iterator)
print('exhausted', list(iterator))
first, iterator = _string.formatter_field_name_split('7' + '.field[2]' * 32)
print('first', first)
gc.collect()
consume(iterator)
print('exhausted', list(iterator))
