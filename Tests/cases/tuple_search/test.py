import gc

values = (1, 2, 1, 3)
print(values.count(1), values.count(8), values.index(1), values.index(1, 1))
print(values.index(1, -2), values.index(3, -(10 ** 100), 10 ** 100))

class Bound:
    def __index__(self):
        gc.collect()
        return 1

print(values.index(1, Bound()))
for operation in (lambda: values.index(8), lambda: values.index(1, 4),
                  lambda: values.index(1, 1, -2), lambda: values.index(1, None),
                  lambda: values.index(1, 1.5), lambda: values.index(1, 0, 4, 5),
                  lambda: tuple.count([], 1), lambda: tuple.index([], 1)):
    try:
        operation()
    except (TypeError, ValueError) as exc:
        print(type(exc).__name__)

class Result:
    def __bool__(self):
        gc.collect()
        return True

class Item:
    def __eq__(self, other):
        gc.collect()
        return Result()

print((Item(), Item()).count(object()), (Item(),).index(object()))

class Broken:
    def __eq__(self, other):
        gc.collect()
        raise RuntimeError('equality')

broken = Broken()
print((broken,).count(broken), (broken,).index(broken))
for operation in (lambda: (broken,).count(1), lambda: (broken,).index(1)):
    try:
        operation()
    except RuntimeError as exc:
        print(type(exc).__name__, str(exc))

class Tuple(tuple):
    def __iter__(self):
        raise AssertionError
    def __getitem__(self, index):
        raise AssertionError

print(Tuple(values).count(1), Tuple(values).index(3))
