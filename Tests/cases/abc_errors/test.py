import abc
import _abc

class A(abc.ABC):
    pass

class B(abc.ABC):
    pass

print(issubclass(B, A))
before = abc.get_cache_token()
A.register(B)
print(abc.get_cache_token() > before)
print(issubclass(B, A))
try:
    B.register(A)
except RuntimeError as error:
    print(str(error))
try:
    A.register(1)
except TypeError as error:
    print(str(error))

class BadHook(abc.ABC):
    @classmethod
    def __subclasshook__(cls, sub):
        return 42

try:
    issubclass(A, BadHook)
except AssertionError as error:
    print(str(error))

_abc._reset_registry(A)
_abc._reset_caches(A)
print(len(_abc._get_dump(A)[0]))
print(issubclass(B, A))
