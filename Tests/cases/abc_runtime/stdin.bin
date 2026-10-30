import abc
import _abc
import _weakref
import gc

class Base(abc.ABC):
    @abc.abstractmethod
    def run(self):
        pass

try:
    Base()
except TypeError:
    print('abstract')

class Concrete(Base):
    def run(self):
        return 7

print(Concrete().run())
class Virtual:
    pass
print(issubclass(Virtual, Base))
token = abc.get_cache_token()
print(Base.register(Virtual) is Virtual)
print(abc.get_cache_token() > token)
print(issubclass(Virtual, Base))
print(isinstance(Virtual(), Base))
print(len(_abc._get_dump(Base)[0]))
ref = _weakref.ref(Virtual)
del Virtual
gc.collect()
print(ref() is None)
print(len(_abc._get_dump(Base)[0]))
_abc._reset_caches(Base)
print(len(_abc._get_dump(Base)[1]))
