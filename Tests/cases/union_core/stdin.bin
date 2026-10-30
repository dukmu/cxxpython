UnionType = type(int | str)
u = int | str
print(UnionType.__module__, UnionType.__name__)
print(repr(u), str(u), u.__args__, u.__parameters__)
print(int | int is int, int | None, None | int)
print((int | None).__args__ == (int, type(None)))
print(int | (str | float), (int | str) | (float | str))
print((u | int) is u, (u | (str | int)) is u)
print(u == (str | int), u != (str | int), u == int)
print(hash(u) == hash(str | int), hash(u) == hash(frozenset(u.__args__)))
print(list[int] | list[int] == list[int])
print(list[int] | None, None | list[int])
print((list[int] | str | list[int]).__args__ == (list[int], str))
print(u.__or__(42) is NotImplemented, u.__ror__(42) is NotImplemented)

class Example:
    pass
Example.__module__ = 'sample'
print(Example | int)

for action in (lambda: UnionType(), lambda: setattr(u, '__args__', (float,)), lambda: u[int]):
    try:
        action()
    except (TypeError, AttributeError) as e:
        print(type(e).__name__)
