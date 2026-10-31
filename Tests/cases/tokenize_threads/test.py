import _thread
import _tokenize
import gc
import io


lock = _thread.allocate_lock()
finished = _thread.allocate_lock()
finished.acquire()
source = 'first = 1\nsecond = (2 + 3)\n# end\n'
lines = io.StringIO(source)
callback_count = [0]
callback_failures = [0]


def reader():
    callback_count[0] += 1
    gc.collect()
    try:
        if callback_count[0] == 2:
            raise ValueError('caught inside callback')
    except ValueError:
        callback_failures[0] += 1
    return lines.readline()


shared = _tokenize.TokenizerIter(reader, extra_tokens=True)
observed = []
failures = []
completed = [0]


def worker():
    try:
        while True:
            with lock:
                try:
                    item = next(shared)
                except StopIteration:
                    break
                except Exception as exc:
                    failures.append((type(exc).__name__, exc.args))
                    continue
                observed.append(item)
        own = list(_tokenize.TokenizerIter(io.StringIO(source).readline, extra_tokens=True))
        with lock:
            if own != expected:
                failures.append(('independent mismatch',))
    finally:
        with lock:
            completed[0] += 1
            if completed[0] == 4:
                finished.release()


expected = list(_tokenize.TokenizerIter(io.StringIO(source).readline, extra_tokens=True))
for i in range(4):
    _thread.start_new_thread(worker, ())
finished.acquire()
print('shared equals reference', observed == expected)
print('tokens', repr(observed))
print('callback', repr((callback_count[0], callback_failures[0])))
print('failures', repr(failures))
print('finally completed', completed[0])
