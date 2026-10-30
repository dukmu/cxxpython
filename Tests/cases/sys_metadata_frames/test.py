import sys
import _thread
import gc

print(isinstance(sys.implementation.name, str), hasattr(sys.implementation, 'cache_tag'))
def outer():
    return inner()
def inner():
    frame = sys._getframe()
    print(frame.f_code.co_name, sys._getframe(1).f_code.co_name)
    print(frame.f_back is sys._getframe(1), sys._getframe(-1) is frame)
    print(sys._getframemodulename(), sys._getframemodulename(depth=1))
    print(sys._getframemodulename(10000) is None)
    try:
        sys._getframe(10000)
    except ValueError as exc:
        print(type(exc).__name__, str(exc))

outer()
inner.__module__ = 'changed'
inner()
del inner.__module__
print(inner.__module__ is None)
print(sys._getframemodulename() == __name__)
def module_value():
    return sys._getframemodulename()
module_value.__module__ = 42
print(module_value())
del module_value.__module__
print(module_value() is None)
for depth in (1 << 100, 1.5):
    try:
        sys._getframe(depth)
    except (OverflowError, TypeError) as exc:
        print(type(exc).__name__)

done = _thread.allocate_lock()
done.acquire()
result = []
def worker():
    gc.collect()
    result.append(sys._getframe().f_code.co_name)
    result.append(sys._getframemodulename())
    done.release()
_thread.start_new_thread(worker, ())
done.acquire()
print(result)
