import gc
gc.disable()
events = []
class C:
    def __del__(self):
        gc.collect()
        events.append(gc.is_finalized(self))
a = C()
b = C()
a.peer = b
b.peer = a
del a, b
gc.collect()
gc.collect()
print(events)
events = []
class A: pass
class B(A): pass
def first(self): events.append('first')
def second(self): events.append('second')
obj = B()
obj.cycle = obj
A.__del__ = first
A.__del__ = second
del obj
gc.collect()
other = B()
other.cycle = other
del A.__del__
del other
gc.collect()
print(events)
