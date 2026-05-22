class Counter:
    def __init__(self, limit):
        self.limit = limit
        self.value = 0

    def __iter__(self):
        return self

    def __next__(self):
        if self.value >= self.limit:
            raise StopIteration()
        current = self.value
        self.value = self.value + 1
        return current


print(list(Counter(3)))

items = []
for value in Counter(2):
    items.append(value)
print(items)

it = iter(Counter(1))
print(next(it))

class OnlyIter:
    def __iter__(self):
        return [1, 2].__iter__()


print(list(OnlyIter()))

class BaseIterable:
    def __iter__(self):
        return [7].__iter__()


class ChildIterable(BaseIterable):
    pass


print(list(ChildIterable()))

class LaterIterable:
    pass


def later_iter(self):
    return [9].__iter__()


LaterIterable.__iter__ = later_iter
print(list(LaterIterable()))
del LaterIterable.__iter__
try:
    list(LaterIterable())
except TypeError as exc:
    print(str(exc).__contains__('not iterable'))
