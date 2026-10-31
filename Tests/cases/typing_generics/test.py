import gc
import typing
from typing import Generic, TypeVar, ParamSpec, TypeVarTuple

T = TypeVar('T')
P = ParamSpec('P')
Ts = TypeVarTuple('Ts')
print(typing.cast(int, 'value'), typing.get_origin(list[int]) is list)
print(typing.get_args(dict[str, int]), typing.get_args(typing.Optional[int]))
print(typing.get_origin(typing.List[int]) is list)

class Box(Generic[T]):
    def __init__(self, value: T):
        self.value = value

item = Box[int](42)
print(Box.__parameters__ == (T,), item.value, item.__orig_class__ == Box[int])
print(typing.get_origin(Box[str]) is Box, typing.get_args(Box[str]))

class NewBox[U]:
    def __init__(self, value: U):
        self.value = value

print(NewBox.__type_params__ == NewBox.__parameters__, NewBox[int](9).value)
print(typing.get_args(NewBox[str]))

type Pair[A] = tuple[A, A]
print(Pair.__parameters__ == Pair.__type_params__, Pair.__value__)
print(typing.get_origin(Pair[int]) is Pair, typing.get_args(Pair[int]))

class Callback(Generic[P]):
    pass

class Shape(Generic[*Ts]):
    pass

print(typing.get_args(Callback[[str, int]]), typing.get_args(Shape[str, int]))
print(typing.get_args(T | int), typing.get_origin(T | int) is typing.Union)

def annotated(value: 'Box[int]') -> 'list[str]':
    return []

hints = typing.get_type_hints(annotated)
print(hints['value'] == Box[int], hints['return'] == list[str])
gc.collect()
print(item.value, Box[int] == item.__orig_class__)
