import _abc
import _thread
import abc
import importlib

class Root(abc.ABC):
    pass
class Virtual:
    pass

Root.register(Virtual)
before = _abc.get_cache_token()
old_data = type(Root._abc_impl)
old_lock = _thread.LockType
lock = _thread.allocate_lock()
importlib.reload(_abc)
importlib.reload(_thread)
print(_abc.get_cache_token() == before)
print(_thread.LockType is old_lock, type(lock) is _thread.LockType)
print(issubclass(Virtual, Root), len(_abc._get_dump(Root)[0]))
class Later(abc.ABC):
    pass
print(type(Later._abc_impl) is old_data)
Later.register(int)
print(_abc.get_cache_token() == abc.get_cache_token(), _abc.get_cache_token() > before)
print(lock.acquire(False))
lock.release()
