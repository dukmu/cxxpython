def first(*args):
    "first doc"
    return "first", len(args)

def second(*args):
    "second doc"
    return "second", len(args)

for factory in (staticmethod, classmethod):
    wrapper = factory.__new__(factory)
    print(hasattr(wrapper, "__func__"), wrapper.__isabstractmethod__)
    try:
        wrapper.__get__(None, object)
    except RuntimeError:
        print("uninitialized")
    wrapper.__init__(first)
    print(wrapper.__func__ is first, wrapper.__wrapped__ is first)
    print(wrapper.__name__, wrapper.__doc__)
    class C:
        method = wrapper
    print(C.method(), C().method())
    wrapper.__init__(second)
    print(wrapper.__func__ is second, wrapper.__name__, wrapper.__doc__)
    print(C.method(), C().method())

class Marked:
    @classmethod
    def method(cls):
        return cls

Marked.method.__func__.marker = 42
print(Marked.method.marker, Marked().method.marker)
for descriptor in (object.__dict__["__new__"], dict.__dict__["fromkeys"]):
    try:
        descriptor.__init__(lambda *args: 7)
    except (TypeError, AttributeError):
        pass
print(type(object()) is object, dict.fromkeys(("key",)) == {"key": None})
