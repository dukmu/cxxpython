import gc
import threading


class Descriptor:
    def __get__(self, instance, owner):
        gc.collect()
        return "descriptor"

    def __set__(self, instance, value):
        gc.collect()
        instance.saved = value

    def __delete__(self, instance):
        gc.collect()
        del instance.saved


class Meta(type):
    def __getattribute__(cls, name):
        gc.collect()
        return type.__getattribute__(cls, name)


class Example(metaclass=Meta):
    item = Descriptor()
    marker = "class"

    def __getattr__(self, name):
        gc.collect()
        if name == "missing":
            return "fallback"
        raise AttributeError(name)


class Other:
    marker = "other"


obj = Example()
obj.item = [1, 2]
print(obj.item, obj.saved, Example.marker, obj.missing)
del obj.item
try:
    obj.saved
except AttributeError:
    print("deleted")


class Errors:
    def __getattribute__(self, name):
        gc.collect()
        raise ValueError("get")

    def __getattr__(self, name):
        return "wrong fallback"

    def __setattr__(self, name, value):
        gc.collect()
        raise ValueError("set")

    def __delattr__(self, name):
        gc.collect()
        raise ValueError("delete")


bad = Errors()
for operation in ("get", "set", "delete"):
    try:
        if operation == "get":
            bad.field
        elif operation == "set":
            bad.field = object()
        else:
            del bad.field
    except ValueError as error:
        print(str(error))

obj.__class__ = Other
print(obj.marker)

errors = []


def worker():
    try:
        local = Example()
        for i in range(10):
            local.item = i
            if local.item != "descriptor" or local.saved != i:
                errors.append("value")
            del local.item
            if local.missing != "fallback":
                errors.append("fallback")
    except BaseException as error:
        errors.append(type(error).__name__)


threads = [threading.Thread(target=worker) for i in range(2)]
for thread in threads:
    thread.start()
for thread in threads:
    thread.join()
print("thread errors", errors)
