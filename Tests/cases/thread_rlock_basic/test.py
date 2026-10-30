import _thread

lock = _thread.RLock()
print(type(lock) is _thread.RLock)
print(lock._is_owned(), lock._recursion_count())
print('unlocked' in repr(lock), 'owner=0 count=0' in repr(lock))
print(lock.acquire(), lock.acquire(False))
print(lock._is_owned(), lock._recursion_count())
print('locked' in repr(lock), 'count=2' in repr(lock))
state = lock._release_save()
print(state[0], state[1] == _thread.get_ident(), lock._is_owned())
lock._acquire_restore(state)
print(lock._recursion_count())
lock.release()
with lock as acquired:
    print(acquired, lock._recursion_count())
lock.release()
for action in (lock.release, lock._release_save):
    try:
        action()
    except RuntimeError as exc:
        print(type(exc).__name__, str(exc))
for args in ((False, 1), (True, -2), (True, float('nan'))):
    try:
        lock.acquire(*args)
    except (ValueError, OverflowError) as exc:
        print(type(exc).__name__)
print(type(_thread.RLock(1, ignored=True)) is _thread.RLock)
class Derived(_thread.RLock):
    pass
print('<unlocked Derived object' in repr(Derived()))
try:
    lock._at_fork_reinit()
except NotImplementedError:
    pass
print(lock._recursion_count())
