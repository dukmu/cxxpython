import _thread
import gc
import unicodedata as u

finished = [_thread.allocate_lock() for _ in range(5)]
for lock in finished:
    lock.acquire()
results = [None] * 4
errors = []

def worker(index):
    try:
        text = ('\u212b\ufb01\u1100\u1161\u11a8' * 100)
        for _ in range(20):
            result = u.normalize('NFKC', text)
            assert u.is_normalized('NFKC', result)
            assert u.category('\u212b') == 'Lu'
        long_text = 'a' * 100000 + '\u0315\u0300'
        assert u.normalize('NFD', long_text).endswith('\u0300\u0315')
        try:
            u.lookup('x' * 100000)
        except KeyError as exc:
            assert str(exc) == "'name too long'"
        else:
            raise AssertionError('lookup accepted an oversized name')
        results[index] = len(result)
    except BaseException as exc:
        errors.append(type(exc).__name__)
    finally:
        finished[index].release()

def collector():
    try:
        for _ in range(20):
            gc.collect()
    finally:
        finished[4].release()

for i in range(4):
    _thread.start_new_thread(worker, (i,))
_thread.start_new_thread(collector, ())
for lock in finished:
    lock.acquire()
print(results)
print(errors)
