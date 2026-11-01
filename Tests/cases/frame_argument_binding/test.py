import _thread
import gc


def mixed(a, /, b=2, *rest, c=3, **extra):
    return a, b, rest, c, extra


print(mixed(1))
print(mixed(1, 4, 5, 6, c=7, a=8, x=9))
mixed.__defaults__ = (20,)
mixed.__kwdefaults__["c"] = 30
print(mixed(1))
mixed.__kwdefaults__ = {"c": 40}
print(mixed(1, b=10))


def closure(a, /, b=2, *, c=3):
    def read():
        return a, b, c
    gc.collect()
    return read


print(closure(11, c=13)())


class Example:
    def method(self, a, /, b=2, *, c=3):
        return a, b, c


print(Example().method(1, c=4))


class Name(str):
    pass


print(mixed(1, **{Name("b"): 12, Name("c"): 14}))


def generate(a, /, b=2, *rest, c=3, **extra):
    yield a, b, rest, c, extra


print(next(generate(1, 4, 5, c=6, x=7)))


def errors():
    calls = [
        lambda: mixed(),
        lambda: closure(a=1),
        lambda: closure(1, 2, 3),
        lambda: closure(1, 2, b=3),
        lambda: closure(1, unknown=2),
        lambda: mixed(1, **{1: 2}),
        lambda: generate(),
    ]
    result = []
    for call in calls:
        try:
            call()
        except TypeError:
            result.append("TypeError")
        else:
            result.append("no error")
    return result


print(errors())

done = [_thread.allocate_lock() for _ in range(4)]
for lock in done:
    lock.acquire()
results = [None] * 4


def worker(index):
    try:
        for i in range(50):
            assert mixed(i, c=i + 1, x=i) == (i, 20, (), i + 1, {"x": i})
            assert closure(i, c=i + 1)() == (i, 2, i + 1)
            assert next(generate(i, c=i + 1)) == (i, 2, (), i + 1, {})
            if i % 10 == 0:
                gc.collect()
        results[index] = "ok"
    except BaseException as error:
        results[index] = type(error).__name__
    finally:
        done[index].release()


for i in range(4):
    _thread.start_new_thread(worker, (i,))
for lock in done:
    lock.acquire()
print(results)
