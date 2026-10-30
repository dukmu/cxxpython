import gc, _weakref
gc.disable()
class Target: pass
class Ref(_weakref.ref): pass
target = Target()
a = _weakref.ref(target)
b = _weakref.ref(target, None)
c = Ref(target)
d = Ref(target)
callback = lambda ref: None
e = _weakref.ref(target, callback)
print(a is b, c is not d, a() is target, e.__callback__ is callback,
      a.__callback__ is None, _weakref.getweakrefcount(target),
      len(_weakref.getweakrefs(target)), _weakref.ref is _weakref.ReferenceType)
for fn in (lambda: _weakref.ref(), lambda: _weakref.ref(target, None, None),
           lambda: _weakref.ref(object=target), lambda: a(1),
           lambda: a(value=1), lambda: _weakref.ref([])):
    try:
        fn()
    except TypeError as error:
        print(type(error).__name__)
events = []
other = Target()
other.cycle = other
def cb(ref): events.append(ref)
try:
    _weakref.ref(other, cb, unexpected=1)
except TypeError as error:
    print(type(error).__name__, _weakref.getweakrefcount(other))
del other
gc.collect()
print(events)
