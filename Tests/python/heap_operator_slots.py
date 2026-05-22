class Left:
    def __add__(self, other):
        return 'left-add'

    def __sub__(self, other):
        return NotImplemented

    def __lt__(self, other):
        return 'left-lt'


class Right:
    def __radd__(self, other):
        return 'right-radd'

    def __rsub__(self, other):
        return 'right-rsub'

    def __lt__(self, other):
        return 'right-lt'


print(Left() + Right())
print(Left() - Right())
print(Left() < Right())
print(Left() > Right())


class Base:
    def __add__(self, other):
        return 'base-add'

    def __eq__(self, other):
        return 'base-eq'


class Child(Base):
    pass


print(Child() + 1)
print(Child() == 1)


class Later:
    pass


def later_add(self, other):
    return other + 10


Later.__add__ = later_add
print(Later() + 5)
del Later.__add__
try:
    Later() + 5
except TypeError as exc:
    print(str(exc).__contains__('unsupported operand'))


class Inplace:
    def __iadd__(self, other):
        self.value = other
        return self


item = Inplace()
item += 7
print(item.value)


class Callable:
    def __call__(self, first, second, *args, **kwargs):
        print(first)
        print(second)
        print(args)
        print(kwargs)
        return 'called'


print(Callable()('a', 'b', 'c', named='value'))


class LaterCallable:
    pass


def later_call(self):
    return 'later-call'


try:
    LaterCallable()()
except TypeError as exc:
    print(str(exc).__contains__('not callable'))
LaterCallable.__call__ = later_call
print(LaterCallable()())
del LaterCallable.__call__
try:
    LaterCallable()()
except TypeError as exc:
    print(str(exc).__contains__('not callable'))


class Sneaky:
    def __getattribute__(self, name):
        if name == '__add__':
            return sneaky_add
        return object.__getattribute__(self, name)


def sneaky_add(other):
    return 'wrong-path'


try:
    Sneaky() + 1
except TypeError as exc:
    print(str(exc).__contains__('unsupported operand'))
