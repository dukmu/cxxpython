import gc, _weakref
gc.disable()
class Target:
    def __eq__(self, other): return 'equal-result'
    def __ne__(self, other): return 'unequal-result'
left = Target()
left.cycle = left
right = Target()
a = _weakref.ref(left)
b = _weakref.ref(right)
print(a.__eq__(b), a.__ne__(b), a.__eq__(42) is NotImplemented)
live_repr = repr(a)
events = []
def first(ref): events.append(1)
def second(ref): events.append(2)
x = _weakref.ref(left, first)
y = _weakref.ref(left, second)
ordering = _weakref.getweakrefs(left)
print(ordering[0] is a, ordering[1] is y, ordering[2] is x)
del left
gc.collect()
print(live_repr.startswith('<weakref at '), "; to 'Target' at " in live_repr,
      repr(a).endswith('; dead>'), a != b, events,
      _weakref.getweakrefcount([]), _weakref.getweakrefs([]))
