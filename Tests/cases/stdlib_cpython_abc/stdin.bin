# Adapted from CPython 3.12.14 Lib/test/test_abc.py; PSF license.
# Keep the original class behavior, using direct assertions until unittest lands.
import abc

class PropertyBase(metaclass=abc.ABCMeta):
    @abc.abstractproperty
    def foo(self):
        return 3

try:
    PropertyBase()
except TypeError:
    pass
else:
    assert False

class PropertyDerived(PropertyBase):
    @property
    def foo(self):
        return super().foo

assert PropertyDerived().foo == 3
assert not getattr(PropertyDerived.foo, "__isabstractmethod__", False)

class ClassBase(abc.ABC):
    @classmethod
    @abc.abstractmethod
    def foo(cls):
        return cls.__name__

class ClassDerived(ClassBase):
    @classmethod
    def foo(cls):
        return super().foo()

assert type(ClassBase) is abc.ABCMeta
assert ClassDerived.foo() == "ClassDerived"
assert ClassDerived().foo() == "ClassDerived"

class StaticBase(metaclass=abc.ABCMeta):
    @abc.abstractstaticmethod
    def foo():
        return 3

class StaticDerived(StaticBase):
    @staticmethod
    def foo():
        return 4

assert StaticDerived.foo() == 4
assert StaticDerived().foo() == 4

class A(metaclass=abc.ABCMeta):
    pass
class B(metaclass=abc.ABCMeta):
    pass

A.register(B)
assert issubclass(B, A)
assert A.register(A) is A
try:
    B.register(A)
except RuntimeError:
    pass
else:
    assert False

class Virtual(abc.ABC):
    @abc.abstractmethod
    def method(self):
        pass

Virtual.register(int)
assert issubclass(bool, Virtual)
assert isinstance(5, Virtual)
assert not hasattr(5, "method")

class Dynamic(abc.ABC):
    @abc.abstractmethod
    def foo(self):
        pass

Dynamic.foo = lambda self: 42
abc.update_abstractmethods(Dynamic)
assert not Dynamic.__abstractmethods__
assert Dynamic().foo() == 42
print("abc upstream class contracts passed")
