import _random
import gc
import threading

r = _random.Random(173)
errors = []

def worker():
    try:
        for i in range(100):
            value = r.getrandbits(128)
            if value < 0 or value.bit_length() > 128:
                errors.append('bad bits')
            if not 0.0 <= r.random() < 1.0:
                errors.append('bad float')
            if i % 10 == 0:
                gc.collect()
                if len(r.getstate()) != 625:
                    errors.append('bad state')
    except Exception as exc:
        errors.append(type(exc).__name__)

threads = [threading.Thread(target=worker, daemon=True) for i in range(4)]
for thread in threads:
    thread.start()
for thread in threads:
    thread.join(30)
print([thread.is_alive() for thread in threads], errors)

reference = _random.Random(173)
expected = [reference.getrandbits(65536), reference.getrandbits(65536)]
shared = _random.Random(173)
results = [None, None]

def large(index):
    try:
        results[index] = shared.getrandbits(65536)
    except Exception as exc:
        errors.append(type(exc).__name__)

threads = [threading.Thread(target=large, args=(i,), daemon=True) for i in range(2)]
for thread in threads:
    thread.start()
gc.collect()
for thread in threads:
    thread.join(30)
print([thread.is_alive() for thread in threads],
      results == expected or results == expected[::-1], errors)
