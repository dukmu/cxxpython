import gc


class Operand:
    def __init__(self, name):
        self.name = name

    def __neg__(self):
        gc.collect()
        return "neg " + self.name

    def __bool__(self):
        gc.collect()
        return bool(self.name)

    def __len__(self):
        gc.collect()
        return len(self.name)

    def __eq__(self, other):
        gc.collect()
        return self.name == other.name

    def __add__(self, other):
        gc.collect()
        return NotImplemented

    def __radd__(self, other):
        gc.collect()
        return "reflected " + self.name

    def __iadd__(self, other):
        gc.collect()
        return Operand(self.name + other.name)

    def __getitem__(self, key):
        gc.collect()
        return self.name + "[" + key.name + "]"

    def __setitem__(self, key, value):
        gc.collect()
        print("set", self.name, key.name, value.name)

    def __delitem__(self, key):
        gc.collect()
        print("del", self.name, key.name)


print(-Operand("temporary"), bool(Operand("yes")), len(Operand("abc")))
print(Operand("same") == Operand("same"))
print(1 + Operand("right"))
value = Operand("in")
value += Operand("place")
print(value.name)
print(Operand("box")[Operand("key")])
Operand("box")[Operand("key")] = Operand("value")
del Operand("box")[Operand("key")]


class Iterator:
    def __iter__(self):
        gc.collect()
        return self

    def __next__(self):
        gc.collect()
        return "first"


iterator = iter(Iterator())
print(next(iterator))


def replacement(self):
    gc.collect()
    raise StopIteration("finished")


Iterator.__next__ = replacement
try:
    next(iterator)
except StopIteration as error:
    print("stop", error.value)


def failure(self):
    gc.collect()
    raise ValueError("failure")


Iterator.__next__ = failure
try:
    next(iterator)
except ValueError as error:
    print("error", error)
