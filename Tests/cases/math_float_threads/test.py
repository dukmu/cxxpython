import gc
import math
import threading

errors = []

class Number:
    def __float__(self):
        gc.collect()
        return -1.25

def worker():
    try:
        for i in range(20):
            if math.modf(Number()) != (-0.25, -1.0):
                errors.append('modf')
            if math.frexp(math.ldexp(0.75, 4)) != (0.75, 4):
                errors.append('frexp')
    except Exception as error:
        errors.append(type(error).__name__)

threads = [threading.Thread(target=worker, daemon=True) for i in range(3)]
for thread in threads:
    thread.start()
for thread in threads:
    thread.join(30)
print([thread.is_alive() for thread in threads], errors)
