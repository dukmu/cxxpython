import gc
import _thread


class Sample:
    def __getattr__(self, name):
        return "fallback:" + name


item = Sample()
print(item.missing, item.missing)
Sample.__getattr__ = lambda self, name: "replacement:" + name
print(item.missing)
del Sample.__getattr__
try:
    item.missing
except AttributeError:
    print("missing")


class Access:
    def __getattribute__(self, name):
        if name == "bad":
            raise ValueError("bad")
        raise AttributeError(name)

    def __getattr__(self, name):
        return "handled:" + name


access = Access()
print(access.ok)
try:
    access.bad
except ValueError as exc:
    print(type(exc).__name__, str(exc))
Access.__getattr__ = None
try:
    access.ok
except TypeError:
    print("none fallback")


class Method:
    def __init__(self, label):
        self.label = label

    def __get__(self, obj, owner):
        label = self.label
        return lambda *args: print(label, *args)


class Managed:
    __setattr__ = Method("set")
    __delattr__ = Method("delete")


managed = Managed()
managed.field = 1
del managed.field
Managed.__setattr__ = Method("new set")
gc.collect()
managed.field = 2
del Managed.__setattr__
managed.field = 3
print(managed.field)
del Managed.__delattr__
del managed.field
print(hasattr(managed, "field"))


class Meta(type):
    def __getattribute__(cls, name):
        if name == "answer":
            return "meta"
        return type.__getattribute__(cls, name)


class WithMeta(metaclass=Meta):
    answer = "class"


print(WithMeta.answer, WithMeta.answer)
Meta.__getattribute__ = lambda cls, name: "changed"
print(WithMeta.answer)
del Meta.__getattribute__
print(WithMeta.answer)


class Reader:
    __getattribute__ = Method("get")


reader = Reader()
print(reader.value)
Reader.__getattribute__ = None
try:
    reader.value
except TypeError:
    print("none get")
del Reader.__getattribute__
print(reader.__class__.__name__)


class Concurrent:
    def __getattribute__(self, name):
        return "first"


def second(self, name):
    return "second"


first = Concurrent.__getattribute__
shared = Concurrent()
errors = []
done = _thread.allocate_lock()
done.acquire()


def read_shared():
    try:
        for n in range(200):
            if shared.value not in ("first", "second"):
                errors.append("unexpected")
    except BaseException as exc:
        errors.append(type(exc).__name__)
    finally:
        done.release()


_thread.start_new_thread(read_shared, ())
for n in range(100):
    Concurrent.__getattribute__ = second
    Concurrent.__getattribute__ = first
    if n % 20 == 0:
        gc.collect()
done.acquire()
print("concurrent", errors)
