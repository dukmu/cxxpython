from dataclasses import dataclass, field, InitVar, replace


def check(name, function):
    try:
        print(name, function())
    except Exception as error:
        print(name, type(error).__name__, str(error))


@dataclass(frozen=True)
class Frozen:
    """A frozen base."""
    x: int


def nonfrozen_child():
    @dataclass
    class Child(Frozen):
        y: int
    return Child(1, 2)


@dataclass
class Plain:
    """A non-frozen base."""
    x: int


def frozen_child():
    @dataclass(frozen=True)
    class Child(Plain):
        y: int
    return Child(1, 2)


@dataclass
class WithInit:
    """A required initialization-only field."""
    x: int
    y: InitVar[int]
    z: int = field(init=False)

    def __post_init__(self, y):
        self.z = self.x + y


check('nonfrozen', nonfrozen_child)
check('frozen', frozen_child)
check('replace-initvar', lambda: replace(WithInit(1, 2), x=4))
check('replace-initvar-value', lambda: replace(WithInit(1, 2), x=4, y=3))
check('field-conflict', lambda: field(default=1, default_factory=list))


@dataclass(slots=True)
class SlotBase:
    """A slotted base."""
    x: int


@dataclass(slots=True)
class SlotChild(SlotBase):
    """Re-declare an inherited slot as a field."""
    x: int
    y: int


print('slot-inheritance', SlotChild.__slots__, SlotChild(1, 2))
