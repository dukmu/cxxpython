import gc
import _weakref

values = iter([1, 2, None, 3])
iterator = iter(lambda: next(values), None)
print(type(iterator).__name__, iter(iterator) is iterator, list(iterator), next(iterator, 'done'))

class Sentinel:
    def __eq__(self, other):
        gc.collect()
        print('compare', other)
        return other == 2

values = iter([1, 2, 3])
print(list(iter(lambda: next(values), Sentinel())))

class Producer:
    def __init__(self):
        self.number = 0

    def __call__(self):
        self.number += 1
        gc.collect()
        if self.number == 1:
            raise ValueError('retry')
        if self.number == 3:
            raise StopIteration('end')
        return self.number

producer = Producer()
ref = _weakref.ref(producer)
iterator = iter(producer, None)
del producer
try:
    next(iterator)
except ValueError as exc:
    print(type(exc).__name__)
print(next(iterator), next(iterator, 'done'), next(iterator, 'done'))
gc.collect()
print('released', ref() is None)
for action in (lambda: iter(1, 2), lambda: iter(), lambda: iter(1, 2, 3),
               lambda: iter(iterator=())):
    try:
        action()
    except TypeError as exc:
        print(type(exc).__name__)
