from enum import Flag, IntFlag, auto, STRICT, CONFORM, EJECT, KEEP


class Access(Flag):
    READ = auto()
    WRITE = auto()
    BOTH = READ | WRITE
    EXECUTE = auto()


print([(item.name, item.value) for item in Access])
print(Access.BOTH is (Access.READ | Access.WRITE))
print((Access.BOTH & Access.READ).name, (Access.BOTH ^ Access.READ).name)
print((~Access.READ).value, list(Access.BOTH), len(Access.BOTH))


class Strict(Flag, boundary=STRICT):
    A = 1


class Conform(Flag, boundary=CONFORM):
    A = 1


class Eject(IntFlag, boundary=EJECT):
    A = 1


class Keep(IntFlag, boundary=KEEP):
    A = 1


try:
    Strict(3)
except ValueError as exc:
    print(type(exc).__name__)
print(Conform(3).value, Eject(3), type(Eject(3)).__name__)
print(Keep(3).value, type(Keep(3)).__name__)
