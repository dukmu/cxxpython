import gc
import _weakref as weakref

class Base:
    pass

class Child(Base):
    pass

class Grandchild(Child):
    pass

print(Base.__subclasses__() == [Child])
print(Child.__subclasses__() == [Grandchild])
ref = weakref.ref(Grandchild)
del Grandchild
gc.collect()
print(ref() is None)
print(Child.__subclasses__())
