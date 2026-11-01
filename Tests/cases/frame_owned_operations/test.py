import gc
import sys
import threading


class Number:
    def __init__(self, value):
        self.value = value

    def __add__(self, other):
        gc.collect()
        return self.value + other.value

    def __radd__(self, other):
        gc.collect()
        return other + self.value

    def __iadd__(self, other):
        gc.collect()
        return Number(self.value + other.value)

    def __neg__(self):
        gc.collect()
        return -self.value

    def __bool__(self):
        gc.collect()
        return self.value != 0

    def __lt__(self, other):
        gc.collect()
        return self.value < other.value


class Items:
    def __getitem__(self, key):
        gc.collect()
        return key.value

    def __setitem__(self, key, value):
        gc.collect()
        print('set', key.value, value.value)

    def __delitem__(self, key):
        gc.collect()
        print('delete', key.value)


print('add', Number(2) + Number(3))
print('reflected', 4 + Number(5))
number = Number(6)
number += Number(7)
print('inplace', number.value)
print('unary', -Number(8))
print('get', Items()[Number(9)])
Items()[Number(10)] = Number(11)
del Items()[Number(12)]
if Number(1):
    print('truth', True)
if not Number(0):
    print('false', True)
print('compare', Number(1) < Number(2))


def replacement(self, other):
    gc.collect()
    return self.value * other.value


Number.__add__ = replacement
print('replaced', Number(3) + Number(4))


class Failing:
    def __add__(self, other):
        gc.collect()
        raise ValueError('add failure')

    def __lt__(self, other):
        gc.collect()
        raise ValueError('compare failure')

    def __getitem__(self, key):
        gc.collect()
        raise ValueError('item failure')

    def __bool__(self):
        gc.collect()
        raise ValueError('truth failure')


try:
    Failing() + Number(1)
except ValueError as error:
    print('exception', str(error))
try:
    Failing() < Number(1)
except ValueError as error:
    print('exception', str(error))
try:
    Failing()[Number(1)]
except ValueError as error:
    print('exception', str(error))
try:
    if Failing():
        pass
except ValueError as error:
    print('exception', str(error))


class ClearCaller:
    def __add__(self, other):
        gc.collect()
        try:
            sys._getframe(1).clear()
        except RuntimeError:
            return other.value
        return -1


def caller():
    return ClearCaller() + Number(17)


print('running frame', caller())


def worker(results, errors, index):
    try:
        total = 0
        for i in range(20):
            total += Number(i) + Number(2)
            if Number(i < 10):
                total += Items()[Number(1)]
        results[index] = total
    except BaseException as error:
        errors.append(type(error).__name__)


results = [0, 0, 0, 0]
errors = []
threads = [threading.Thread(target=worker, args=(results, errors, i)) for i in range(4)]
for thread in threads:
    thread.start()
for thread in threads:
    thread.join()
print('threads', results)
print('thread errors', errors)
