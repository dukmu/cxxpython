from enum import Enum, IntEnum, StrEnum, Flag, IntFlag, auto, unique


class Color(Enum):
    RED = 1
    CRIMSON = 1
    BLUE = 2


print(Color.RED.name, Color.RED.value, Color(1) is Color.RED)
print(Color.CRIMSON is Color.RED, list(Color), list(Color.__members__))
print(Color['BLUE'], repr(Color.RED), str(Color.BLUE))


class Number(IntEnum):
    ONE = 1
    TWO = auto()


class Text(StrEnum):
    FIRST = auto()
    SECOND = 'second-value'


print(Number.TWO.value, Number.ONE == 1, Number.ONE + Number.TWO)
print(Text.FIRST.value, str(Text.SECOND), Text.FIRST == 'first')


class Permission(Flag):
    READ = auto()
    WRITE = auto()
    EXECUTE = auto()


class Bits(IntFlag):
    A = auto()
    B = auto()


both = Permission.READ | Permission.WRITE
print(both, Permission.READ in both, list(both), (~both).value)
print((Bits.A | Bits.B).value, (Bits.A | 8).value, int(Bits.B))


try:
    unique(Color)
except ValueError as exc:
    print(type(exc).__name__, str(exc))


Animal = Enum('Animal', 'ANT BEE', start=3)
print(Animal.ANT.value, Animal.BEE.value, Animal(3) is Animal.ANT)
Point = IntEnum('Point', {'LOW': 4, 'HIGH': 8})
print(Point.LOW + Point.HIGH, Point['HIGH'].value)
