import gc
import math
import threading

errors = []

def worker():
    try:
        for i in range(20):
            n = 300 + i
            if math.comb(n, 100) * math.factorial(100) != math.perm(n, 100):
                errors.append('wrong result')
            if math.gcd((1 << n) - 1, (1 << (2 * n)) - 1) != (1 << n) - 1:
                errors.append('wrong gcd')
            gc.collect()
    except Exception as exc:
        errors.append(type(exc).__name__)

threads = [threading.Thread(target=worker, daemon=True) for i in range(3)]
for thread in threads:
    thread.start()
for thread in threads:
    thread.join(30)
print([thread.is_alive() for thread in threads], errors)
