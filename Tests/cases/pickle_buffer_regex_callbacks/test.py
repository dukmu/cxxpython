import gc
import pickle
import re
import threading
import weakref

pattern = re.compile(rb'[a-z]+')


def subject(data):
    view = memoryview(data)[1:-1]
    return pickle.PickleBuffer(view), weakref.ref(view)


def exercise():
    data = bytearray(b'_aa bb cc_')
    source, reference = subject(data)
    spans = []

    def replace(match):
        spans.append(match.span())
        if len(spans) == 1:
            assert match.group() == b'aa'
            data[4:6] = b'12'
            source.release()
        gc.collect()
        assert reference() is not None
        try:
            reference().release()
        except BufferError:
            pass
        else:
            raise AssertionError('callback lost its direct export')
        return b'X'

    result = pattern.sub(replace, source)
    assert result == b'X 12 X' and spans == [(0, 2), (6, 8)]
    gc.collect()
    assert reference() is None
    data.append(33)
    return result, spans


print('callback mutation and release', exercise())
failures = []
results = [False] * 3


def worker(index):
    try:
        for step in range(4):
            assert exercise()[0] == b'X 12 X'
        results[index] = True
    except BaseException as exc:
        failures.append((index, type(exc).__name__, str(exc)))


threads = [threading.Thread(target=worker, args=(i,), daemon=True) for i in range(3)]
for thread in threads:
    thread.start()
for thread in threads:
    thread.join(60)
assert not any(thread.is_alive() for thread in threads), 'worker timed out'
assert not failures, failures
print('collecting callback workers', results, failures)

data = bytearray(b'_aa_')
source, reference = subject(data)


def abort(match):
    source.release()
    gc.collect()
    assert reference() is not None
    raise RuntimeError('replacement failed')


try:
    pattern.sub(abort, source)
except RuntimeError as exc:
    print('callback exception', str(exc))
else:
    raise AssertionError('callback exception lost')
gc.collect()
assert reference() is None
data.append(33)
print('exception releases export', True)
