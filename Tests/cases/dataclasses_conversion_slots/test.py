from dataclasses import asdict, astuple, dataclass, field, make_dataclass, replace
import weakref


@dataclass
class Inner:
    value: int


@dataclass
class Outer:
    inner: Inner
    items: list
    cache: int = field(init=False, default=9)


source = Outer(Inner(3), [Inner(4)])
print(asdict(source))
print(astuple(source))
print(replace(source, inner=Inner(5)))
try:
    replace(source, cache=1)
except ValueError as error:
    print(type(error).__name__, str(error))

Point = make_dataclass('Point', [('x', int), ('y', int, field(default=2))],
                       slots=True, weakref_slot=True)
p = Point(1)
print(p, Point.__slots__, weakref.ref(p)() is p, hasattr(p, '__dict__'))
try:
    make_dataclass('Bad', [], weakref_slot=True)
except TypeError as error:
    print(type(error).__name__, str(error))
