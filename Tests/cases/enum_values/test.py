from enum import Enum, IntEnum, auto, unique


class Sequence(Enum):
    FIRST = auto()
    SECOND = auto()
    _ignore_ = 'temporary'
    temporary = 100
    THIRD = auto()


print([(item.name, item.value) for item in Sequence])
print('temporary' in Sequence.__members__)


class Unhashable(Enum):
    FIRST = [1]
    ALIAS = [1]
    SECOND = [2]


print(Unhashable.ALIAS is Unhashable.FIRST, Unhashable([2]).name)
print(list(Unhashable.__members__))


@unique
class Status(IntEnum):
    OK = 200
    ERROR = 500


print(Status(200), Status.ERROR.name, Status.ERROR.value)
for value in (0, 300):
    try:
        Status(value)
    except ValueError as exc:
        print(type(exc).__name__, str(exc))


class Custom(Enum):
    def _generate_next_value_(name, start, count, last_values):
        return name.lower() + str(count)

    ALPHA = auto()
    BETA = auto()


print(Custom.ALPHA.value, Custom.BETA.value)
