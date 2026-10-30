for operation, value in ((isinstance, 1), (issubclass, int)):
    for candidate in (int | list[int], list[int] | int, str | float, str | int):
        try:
            print(operation(value, candidate))
        except TypeError as e:
            print(type(e).__name__, str(e))
print(isinstance(None, str | None), issubclass(type(None), str | None))

class Meta(type):
    def __instancecheck__(self, value):
        print('instance-hook')
        return True
    def __subclasscheck__(self, value):
        print('subclass-hook')
        return True

class Checked(metaclass=Meta):
    pass
print(isinstance(object(), Checked | int))
print(issubclass(123, Checked | int))

class Pretend:
    @property
    def __class__(self):
        return int
print(isinstance(Pretend(), int | str))

class ClassLike:
    __bases__ = (int,)
print(issubclass(ClassLike(), int | str))

class Broken:
    @property
    def __class__(self):
        raise ValueError('class lookup failed')
try:
    isinstance(Broken(), int | str)
except ValueError as e:
    print(type(e).__name__, str(e))
