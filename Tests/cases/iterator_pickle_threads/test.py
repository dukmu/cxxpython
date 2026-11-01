import gc
import threading
import time


def check(iterator, expected):
    consumed = []
    errors = []
    start = threading.Barrier(2)

    def advance():
        try:
            start.wait()
            for value in iterator:
                consumed.append(value)
                time.sleep(0)
        except BaseException as exc:
            errors.append(exc)

    thread = threading.Thread(target=advance)
    thread.start()
    start.wait()
    try:
        for _ in range(8):
            reduction = iterator.__reduce__()
            gc.collect()
            clone = reduction[0](*reduction[1])
            if len(reduction) == 3:
                clone.__setstate__(reduction[2])
            remaining = list(clone)
            assert remaining == expected[len(expected) - len(remaining):]
    finally:
        thread.join()
    assert not errors
    assert consumed == expected


values = list(range(64))
check(iter(values), values)
check(iter(tuple(values)), values)
check(iter('a' * 64), ['a'] * 64)
check(iter(dict.fromkeys(values)), values)
print('concurrent reductions preserve source progress and rooted snapshots')
