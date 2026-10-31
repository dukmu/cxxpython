events = []
T = "outer"


def mark(value):
    events.append(value)
    return value


def decorate(value):
    events.append("decorator " + value)
    def apply(func):
        events.append("apply " + value)
        return func
    return apply


@decorate(mark("first"))
@decorate(mark("second"))
def func[T, *Ts, **P](x: T = mark(T), *, y=mark("keyword")) -> T:
    return T, x, y


print("events", events)
print("params", func.__type_params__)
print("annotations", func.__annotations__)
print("call", func())
print("outer", T)


def factory(value):
    def inner[T](x: T) -> T:
        return value, T, x
    return inner


f = factory("captured")
print("closure", f(42), f.__qualname__)


async def async_func[T](x: T) -> T:
    return T, x


coro = async_func(7)
try:
    coro.send(None)
except StopIteration as exc:
    print("async", exc.value, async_func.__annotations__)


class Private:
    def method[__T](self, x: __T) -> __T:
        return __T, x


print("private", Private.method.__type_params__, Private.method.__annotations__, Private().method(9))

namespace = {}
exec("from __future__ import annotations\ndef future[T](x: T, y: list[T]) -> T: return T\n", namespace)
future = namespace["future"]
print("future", future.__type_params__, future.__annotations__, future(1, 2))
