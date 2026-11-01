import copyreg
import functools
import io
import multiprocessing.reduction as reduction
import pickle
import socket


class Item:
    def __init__(self, value):
        self.value = value


def rebuild_item(value):
    return Item(value)


def reduce_item(item):
    return rebuild_item, (item.value + 1,)


class Other:
    def __init__(self, value):
        self.value = value


def reduce_other(item):
    return Other, (item.value + 2,)


value = [1, "two", None, {"three": 3}]
print("basic", reduction.ForkingPickler.loads(reduction.ForkingPickler.dumps(value)))

cycle = []
cycle.append(cycle)
restored = pickle.loads(reduction.ForkingPickler.dumps(cycle))
print("cycle", restored is restored[0])

copyreg.pickle(Other, reduce_other)
reduction.register(Item, reduce_item)
file = io.BytesIO()
reduction.dump([Item(10), Other(20)], file)
items = pickle.loads(file.getvalue())
print("reducers", items[0].value, items[1].value)

partial = functools.partial(pow, 2)
restored_partial = pickle.loads(reduction.ForkingPickler.dumps(partial))
print("partial", restored_partial(5))
print("socket registered", socket.socket in reduction.ForkingPickler._extra_reducers)
print("socket reducer", reduction.ForkingPickler._extra_reducers[socket.socket] is reduction._reduce_socket)
