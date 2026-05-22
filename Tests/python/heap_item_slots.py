class Bag:
    def __init__(self):
        self.values = {'a': 1}
        self.deleted = []

    def __getitem__(self, key):
        return self.values[key]

    def __setitem__(self, key, value):
        self.values[key] = value

    def __delitem__(self, key):
        self.deleted.append(key)
        del self.values[key]

    def __contains__(self, key):
        return self.values.__contains__(key)


bag = Bag()
print(bag['a'])
bag['b'] = 2
print(bag['b'])
print('b' in bag)
del bag['a']
print(bag.deleted)
print('a' in bag)


class IterableOnly:
    def __iter__(self):
        return [1, 2, 3].__iter__()


print(2 in IterableOnly())
print(5 in IterableOnly())


class LaterItems:
    pass


def later_get(self, key):
    return key + 10


LaterItems.__getitem__ = later_get
print(LaterItems()[5])
del LaterItems.__getitem__
try:
    LaterItems()[5]
except TypeError as exc:
    print(str(exc).__contains__('subscriptable'))


class ContainsTruth:
    def __contains__(self, key):
        return key


print('x' in ContainsTruth())
print('' in ContainsTruth())


class Indexable:
    def __len__(self):
        return 3

    def __getitem__(self, index):
        return index + 1


print(list(Indexable()))
