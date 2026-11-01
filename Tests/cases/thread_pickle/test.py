import _thread

lock = _thread.allocate_lock()
print("types", type(lock) is _thread.LockType, _thread.LockType.__module__,
      _thread.LockType.__name__, hasattr(_thread, "lock"))
print("unlocked", lock.locked(), repr(lock).startswith("<unlocked _thread.lock object at "))
print("acquire", lock.acquire(False), lock.locked(),
      repr(lock).startswith("<locked _thread.lock object at "))
lock.release()
try:
    _thread.LockType()
except TypeError:
    print("constructor rejected")
try:
    _thread.LockType.extra = 1
except TypeError:
    print("type immutable")
try:
    class Child(_thread.LockType):
        pass
except TypeError:
    print("subclass rejected")

rlock = _thread.RLock()
print("rlock type", type(rlock) is _thread.RLock)
print("recursive", rlock.acquire(), rlock.acquire(), rlock._recursion_count(), rlock._is_owned())
text = repr(rlock)
print("repr state", int(text.split("count=")[1].split()[0].rstrip(">")),
      int(text.split("owner=")[1].split()[0]) == _thread.get_ident())
state = rlock._release_save()
print("released", state[0], state[1] == _thread.get_ident(), rlock._is_owned())
rlock._acquire_restore(state)
print("restored", rlock._recursion_count(), rlock._is_owned())
rlock.release()
rlock.release()
print("final", rlock._recursion_count(), rlock._is_owned())
