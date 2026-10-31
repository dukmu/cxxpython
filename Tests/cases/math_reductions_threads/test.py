import gc
import math
import threading

errors = []
retained = []

class Number:
    def __float__(self):
        gc.collect()
        return 3.0

def stream(values):
    for value in values:
        gc.collect()
        yield value

def worker():
    try:
        for i in range(8):
            assert math.fsum(stream([1e100, Number(), -1e100])) == 3.0
            product = math.prod(stream([2 ** 200, 3, 2 ** 300]))
            assert product == 3 * 2 ** 500
            dot = math.sumprod(stream([2 ** 200, 3]), stream([2 ** 300, 4]))
            assert dot == 2 ** 500 + 12
            assert math.hypot(Number(), 4.0) == 5.0
            assert math.dist(stream([Number(), 4.0]), stream([0, 0])) == 5.0
            retained.append((product, dot))
            gc.collect()
    except Exception as error:
        errors.append(type(error).__name__)

threads = [threading.Thread(target=worker) for i in range(3)]
for thread in threads:
    thread.start()
for thread in threads:
    thread.join()
print(errors, len(retained))
print(all(product == 3 * 2 ** 500 and dot == 2 ** 500 + 12 for product, dot in retained))
