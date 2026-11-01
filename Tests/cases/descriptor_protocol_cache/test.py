import _thread
import gc


class Descriptor:
    def __get__(self, obj, owner):
        gc.collect()
        return "old"

    def __set__(self, obj, value):
        obj.saved = value

    def __delete__(self, obj):
        obj.saved = "deleted"


class Meta(type):
    item = Descriptor()


class Subject(metaclass=Meta):
    item = Descriptor()


obj = Subject()
obj.__dict__["item"] = "instance"
print(obj.item, Subject.item)
obj.item = "set"
print(obj.saved)
del obj.item
print(obj.saved)


def new_get(self, obj, owner):
    gc.collect()
    return "new"


def new_set(self, obj, value):
    obj.saved = "new " + value


def new_delete(self, obj):
    obj.saved = "new deleted"


Descriptor.__get__ = new_get
Descriptor.__set__ = new_set
Descriptor.__delete__ = new_delete
print(obj.item, Subject.item)
obj.item = "set"
print(obj.saved)
del obj.item
print(obj.saved)
del Descriptor.__set__
del Descriptor.__delete__
print(obj.item, Subject.item)
del Descriptor.__get__
print(obj.item, type(Subject.item).__name__)


class NullGet:
    __get__ = None


class NullSet:
    __set__ = None


class NullDelete:
    __delete__ = None


class Nulls:
    get = NullGet()
    set = NullSet()
    delete = NullDelete()


n = Nulls()
for operation in (lambda: n.get, lambda: setattr(n, "set", 1),
                  lambda: delattr(n, "delete")):
    try:
        operation()
    except TypeError:
        print("none raises TypeError")


class SharedDescriptor:
    def __get__(self, obj, owner):
        gc.collect()
        return 42

    def __set__(self, obj, value):
        obj.saved = value

    def __delete__(self, obj):
        obj.saved = 0


class Shared:
    field = SharedDescriptor()


shared = Shared()
done = [_thread.allocate_lock() for _ in range(4)]
errors = [None] * 4
for lock in done:
    lock.acquire()


def worker(index):
    try:
        for i in range(20):
            if shared.field != 42:
                raise ValueError("incorrect descriptor result")
            shared.field = i
            del shared.field
    except BaseException as exc:
        errors[index] = type(exc).__name__
    finally:
        done[index].release()


for i in range(4):
    _thread.start_new_thread(worker, (i,))
for lock in done:
    lock.acquire()
print("threads", errors)
