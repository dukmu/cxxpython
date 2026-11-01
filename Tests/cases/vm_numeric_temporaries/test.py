import _thread
import gc


def identity():
    x = 1.25
    y = 2.5
    a = b = x + y
    print("identity", a is b, a, (x + y) * 4.0 - 1.0)
    x = 1000.5
    print("boxed identity", x is x, (x, x)[0] is x)


identity()
maximum = 9223372036854775807
minimum = -9223372036854775808
print("overflow", (maximum + 1) * 2, minimum - 1)
nan = float("nan")
negative_zero = -0.0
zero = negative_zero * 2.0
print("float", (nan + 1.0) != (nan + 1.0), str(zero), 1.5 + 2, 3 * 2.5)


class Reflected(int):
    def __radd__(self, other):
        return "reflected"


print("fallback", 1 + Reflected(2))
try:
    (negative_zero + 2.0) / 0.0
except ZeroDivisionError as error:
    print("exception", type(error).__name__, error.__traceback__.tb_lineno)


def generator():
    yield (1.5 + 2.5) * 3.0
    gc.collect()
    yield (8 + 9) * 2


print("generator", list(generator()))
chain = eval("lambda x: " + "+".join(["x"] * 100))
print("long expression", chain(0.25))
done = [_thread.allocate_lock() for i in range(4)]
results = [None] * 4
errors = []


def worker(index):
    try:
        value = 0.0
        for i in range(100):
            value = value + chain(i * 2.0 + 1.0)
            if i % 25 == 0:
                gc.collect()
        results[index] = value
    except BaseException as error:
        errors.append(type(error).__name__)
    finally:
        done[index].release()


for lock in done:
    lock.acquire()
for i in range(4):
    _thread.start_new_thread(worker, (i,))
for lock in done:
    lock.acquire()
print("threads", results)
print("thread errors", errors)
