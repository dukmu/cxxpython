import abc
import _abc

class Hook(abc.ABC):
    @classmethod
    def __subclasshook__(cls, sub):
        if sub.__name__ == 'Accepted':
            return True
        return NotImplemented

class Accepted:
    pass

print(issubclass(Accepted, Hook))
print(len(_abc._get_dump(Hook)[1]))

class Abstract(abc.ABC):
    @classmethod
    @abc.abstractmethod
    def method(cls):
        pass

class StillAbstract(Abstract):
    pass

try:
    StillAbstract()
except TypeError:
    print('inherited abstract')

class Concrete(StillAbstract):
    @classmethod
    def method(cls):
        return 'implemented'

print(Concrete.method())
Abstract.__abstractmethods__ = frozenset()
print(Abstract().__class__ is Abstract)

class Ordinary:
    __abstractmethods__ = frozenset(('x',))

print(Ordinary().__class__ is Ordinary)
