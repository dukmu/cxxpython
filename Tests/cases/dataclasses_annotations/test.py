from dataclasses import dataclass, field, fields, InitVar, KW_ONLY
from typing import ClassVar


@dataclass
class Item:
    name: str
    count: InitVar[int]
    kind: ClassVar[str] = 'item'
    _: KW_ONLY
    scale: int = 2
    result: int = field(init=False)

    def __post_init__(self, count):
        self.result = count * self.scale


item = Item('a', 3, scale=4)
print(item, item.kind, item.result)
print([f.name for f in fields(item)])
print(Item.__match_args__)
try:
    Item('a', 3, 4)
except TypeError as error:
    print(type(error).__name__)
