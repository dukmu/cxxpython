from dataclasses import dataclass, field, FrozenInstanceError


@dataclass(frozen=True, order=True)
class Point:
    x: int
    y: int
    label: str = field(compare=False, repr=False)


a = Point(1, 2, 'a')
b = Point(1, 2, 'b')
c = Point(2, 0, 'c')
print(a, a == b, a < c, c > b, hash(a) == hash(b))
try:
    a.x = 4
except FrozenInstanceError as error:
    print(type(error).__name__, str(error))
try:
    del a.y
except FrozenInstanceError as error:
    print(type(error).__name__, str(error))


@dataclass
class Mutable:
    value: int


try:
    hash(Mutable(1))
except TypeError as error:
    print(type(error).__name__)
try:
    dataclass(eq=False, order=True)(type('Bad', (), {}))
except ValueError as error:
    print(type(error).__name__, str(error))
