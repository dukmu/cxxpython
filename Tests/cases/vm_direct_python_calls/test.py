import gc
from types import MethodType


class Payload:
    def __init__(self, label):
        self.label = label

    def method(self, first, *, last):
        gc.collect()
        return self.label, first.label, last.label


def identity(value):
    gc.collect()
    return value.label


print(identity(Payload("direct")))
print(Payload("self").method(Payload("first"), last=Payload("last")))
print(Payload("expanded self").method(*[Payload("expanded arg")],
                                     **{"last": Payload("expanded kw")}))


class Callable:
    def __call__(self, *values, **named):
        gc.collect()
        return [value.label for value in values], named["name"].label


wrapped = MethodType(Callable(), Payload("prefix"))
print(wrapped(Payload("argument"), name=Payload("keyword")))


def generator(value):
    gc.collect()
    yield value.label


saved = generator(Payload("suspended"))
gc.collect()
print(next(saved))


def fail(value):
    gc.collect()
    raise ValueError(value.label)


try:
    fail(Payload("error payload"))
except ValueError as error:
    print(type(error).__name__, str(error))

print(identity(Payload("after error")))
