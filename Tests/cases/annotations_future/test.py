from __future__ import annotations

events = []
def effect():
    events.append('called')
    return int

simple: effect()
complex_value: list[int | None]
expression: (lambda x: x + 1)(3)
other: tuple[int, ...]
class Example:
    __private: effect()
    def method(self, arg: dict[str, list[int]]) -> (int | str):
        pass
print(__annotations__)
print(Example.__annotations__)
print(Example.method.__annotations__)
print(events)
print(bool(Example.method.__code__.co_flags & 16777216))
