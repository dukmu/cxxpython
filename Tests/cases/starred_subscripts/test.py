import gc


events = []


class Items:
    def __init__(self, name, values):
        self.name = name
        self.values = values

    def __getitem__(self, index):
        events.append(("item", self.name, index))
        gc.collect()
        return self.values[index]


class Collector:
    def __getitem__(self, key):
        gc.collect()
        events.append(("get", key))
        return key

    def __setitem__(self, key, value):
        gc.collect()
        events.append(("set", key, value))

    def __delitem__(self, key):
        gc.collect()
        events.append(("del", key))


collector = Collector()


def target():
    events.append("target")
    return collector


def items(name, values):
    events.append(("items", name))
    return Items(name, values)


def value(name, result):
    events.append(name)
    return result


print(target()[*items("one", [1, 2])])
print(events)
events.clear()

print(target()[*items("empty", [])])
print(events)
events.clear()

print(target()[value("first", 0), *items("a", [1]),
               value("start", 2):value("stop", 4):value("step", 1),
               *items("b", [5, 6])])
print(events)
events.clear()

target()[*items("store", [7, 8])] = value("rhs", 9)
print(events)
events.clear()

del target()[*items("delete", [10]), value("last", 11)]
print(events)
events.clear()

print(target()[value("plain", 12)])
print(events)
