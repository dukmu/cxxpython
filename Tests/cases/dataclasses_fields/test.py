from dataclasses import dataclass, field, fields


# Adapted from CPython 3.12.14 test_dataclasses: inherited defaults and factories.
@dataclass
class Base:
    x: int
    items: list = field(default_factory=list)


@dataclass
class Child(Base):
    label: str = 'child'


a = Child(1)
b = Child(2)
a.items.append(3)
print(a, b, a.items is b.items)
print([(f.name, f.init, f.repr) for f in fields(Child)])

calls = []


def factory():
    calls.append('called')
    return [7]


@dataclass
class Defaults:
    x: list = field(default_factory=factory)
    hidden: list = field(default_factory=factory, init=False, repr=False)


obj = Defaults([1])
print(obj, obj.hidden, calls)
try:
    @dataclass
    class Bad:
        x: int = 1
        y: int
except TypeError as error:
    print(type(error).__name__, str(error))
