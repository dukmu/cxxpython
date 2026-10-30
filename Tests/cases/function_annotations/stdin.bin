import gc

events = []
def annotation(value):
    events.append(value)
    gc.collect()
    return value

def outer():
    marker = "closure"
    def inner(a: annotation("a"), /, b=annotation("default"),
              *args: annotation("args"), c: annotation("c")=annotation("kwdefault"),
              **kw: annotation("kw")) -> annotation("return"):
        return marker, a, b, args, c, kw
    return inner

f = outer()
print(events)
print(f.__annotations__)
print(f(1, 2, 3, c=4, d=5))
replacement = {"result": int}
f.__annotations__ = replacement
print(f.__annotations__ is replacement)
del f.__annotations__
print(f.__annotations__, f.__annotations__ is f.__annotations__)
f.__annotations__ = None
print(f.__annotations__)
try:
    f.__annotations__ = []
except TypeError:
    print("TypeError")

class C:
    marker = "class"
    def f(self, __x: marker) -> marker:
        return __x
print(C.f.__annotations__)

def scope():
    local = "parent"
    def annotated(value: local) -> local:
        return value
    return annotated
print(scope().__annotations__)
