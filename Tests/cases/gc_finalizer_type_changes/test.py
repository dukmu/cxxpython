import gc
import weakref

gc.disable()
events = []


class Base:
    pass


class Child(Base):
    pass


class Stable:
    def __del__(self):
        events.append("stable")


def collect_batch(mixed=False):
    refs = []
    for i in range(32):
        item = Child()
        item.cycle = item
        refs.append(weakref.ref(item))
        if mixed:
            item = Stable()
            item.cycle = item
            refs.append(weakref.ref(item))
    del item
    gc.collect()
    print(all(ref() is None for ref in refs))


collect_batch()
print(events)
Base.__del__ = lambda self: events.append("first")
collect_batch()
print(events.count("first"))
Base.__del__ = lambda self: events.append("second")
collect_batch(mixed=True)
print(events.count("first"), events.count("second"), events.count("stable"))
del Base.__del__
collect_batch(mixed=True)
print(events.count("first"), events.count("second"), events.count("stable"))
gc.enable()
