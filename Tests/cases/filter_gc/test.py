import gc
import weakref


class Item:
    def __init__(self, value):
        self.value = value

    def __bool__(self):
        gc.collect()
        return self.value % 2 == 1


class Source:
    def __init__(self):
        self.index = 0

    def __iter__(self):
        gc.collect()
        return self

    def __next__(self):
        if self.index == 6:
            raise StopIteration
        self.index += 1
        gc.collect()
        return Item(self.index)


class Flag:
    def __init__(self, value):
        self.value = value

    def __bool__(self):
        gc.collect()
        return self.value


class Predicate:
    def __call__(self, item):
        gc.collect()
        return Flag(item.value % 2 == 1)


for predicate in (None, bool, Predicate()):
    value = filter(predicate, Source())
    print('callback roots', [item.value for item in value])

source, predicate = Source(), Predicate()
source_ref, predicate_ref = weakref.ref(source), weakref.ref(predicate)
value = filter(predicate, source)
del source, predicate
gc.collect()
print('owns inputs', source_ref() is not None, predicate_ref() is not None)
print('retained execution', [item.value for item in value])
gc.collect()
print('exhausted retains inputs', source_ref() is not None, predicate_ref() is not None)
del value
gc.collect()
print('released inputs', source_ref() is None, predicate_ref() is None)

source, predicate = Source(), Predicate()
value = filter(predicate, source)
source.owner = predicate.owner = value
source_ref, predicate_ref = weakref.ref(source), weakref.ref(predicate)
del source, predicate, value
gc.collect()
print('traced cycle', source_ref() is None, predicate_ref() is None)
