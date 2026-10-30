import _thread
import importlib

kind = _thread.RLock
lock = kind()
lock.acquire()
importlib.reload(_thread)
print(_thread.RLock is kind, type(lock) is _thread.RLock)
print(lock._is_owned(), lock._recursion_count())
lock.release()
