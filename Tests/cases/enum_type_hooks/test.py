events = []


class Member:
    def __init__(self, value):
        self.value = value

    def __set_name__(self, owner, name):
        events.append(('set_name', name, self.value))
        delattr(owner, name)
        setattr(owner, name, self.value)


class Meta(type):
    @classmethod
    def __prepare__(metacls, name, bases, **kwargs):
        events.append(('prepare', name))
        return {}

    def __new__(metacls, name, bases, namespace, **kwargs):
        events.append(('new', name))
        return super().__new__(metacls, name, bases, namespace, **kwargs)


class Base:
    def __init_subclass__(cls, **kwargs):
        events.append(('init_subclass', cls.__name__, cls.FIRST, cls.SECOND))
        super().__init_subclass__(**kwargs)


class Example(Base, metaclass=Meta):
    FIRST = Member(1)
    SECOND = Member(2)


print(events)
print(Example.FIRST, Example.SECOND)


class Broken:
    def __set_name__(self, owner, name):
        raise ValueError('broken descriptor')


try:
    class Failure:
        item = Broken()
except Exception as exc:
    print(type(exc).__name__, str(exc))
    print(exc.__notes__)
