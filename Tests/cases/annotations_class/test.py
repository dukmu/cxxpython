import gc

class Base:
    field: int

class Child(Base):
    pass

print(Base.__annotations__)
print(Child.__annotations__)
print(Child.__annotations__ is Child.__dict__['__annotations__'])
Child.__annotations__ = None
print(Child.__annotations__)
del Child.__annotations__
print(Child.__annotations__)
del Child.__annotations__
try:
    del Child.__annotations__
except AttributeError:
    print('missing annotations')

class Descriptor:
    def __get__(self, instance, owner):
        gc.collect()
        return instance is None, owner is Child

Child.__annotations__ = Descriptor()
print(Child.__annotations__)
for cls in (int, object, type):
    try:
        cls.__annotations__
    except AttributeError:
        print('no builtin annotations')

events = []
class Namespace(dict):
    def __setitem__(self, key, value):
        events.append(key)
        super().__setitem__(key, value)

class Meta(type):
    @classmethod
    def __prepare__(mcls, name, bases):
        return Namespace()

class Ordered(metaclass=Meta):
    'doc'
    field: int

print(events)
