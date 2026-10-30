import _thread

lock = _thread.allocate_lock()
print(type(lock) is _thread.LockType, lock.locked())
print(lock.acquire(False), lock.locked(), lock.acquire(False))
print(lock.acquire(timeout=0))
lock.release()
print(lock.locked(), lock.acquire(timeout=0.001))
lock.release_lock()
with lock as acquired:
    print(acquired, lock.locked())
print(lock.locked())
for args in [(False, 0), (True, -2), (True, _thread.TIMEOUT_MAX * 2)]:
    try:
        lock.acquire(*args)
    except Exception as error:
        print(type(error).__name__)
try:
    lock.release()
except RuntimeError as error:
    print(type(error).__name__, str(error))
print(_thread.get_ident() == _thread.get_ident(), _thread.get_native_id() > 0)
for method in (_thread.LockType.acquire, _thread.LockType.release, _thread.LockType.locked):
    try:
        method(1)
    except TypeError:
        print('TypeError')

class Blocking:
    def __bool__(self):
        return False
print(lock.acquire(1.0))
print(lock.acquire(Blocking()))
lock.release()
