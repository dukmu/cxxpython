from _collections import deque

match deque([1, 2]):
    case [a, b]:
        print('native deque', a, b)
    case _:
        print('deque rejected')

from collections.abc import Mapping, Sequence

events = []


class Seq(Sequence):
    def __len__(self):
        events.append('len')
        return 3

    def __getitem__(self, index):
        events.append(('item', index))
        if index >= 3:
            raise IndexError
        return index + 1


match Seq():
    case [a, *b]:
        print('sequence protocol', a, b, events)


class Pretender:
    def __len__(self):
        return 2

    def __getitem__(self, index):
        if index >= 2:
            raise IndexError
        return index


match Pretender():
    case [_, _]:
        print('pretender matched')
    case _:
        print('pretender rejected')


class RegisteredSeq(Pretender):
    pass


Sequence.register(RegisteredSeq)
match RegisteredSeq():
    case [a, b]:
        print('registered sequence', a, b)


class Map(Mapping):
    def __init__(self):
        self.data = {'a': 1, 'b': 2}

    def __len__(self):
        events.append('map len')
        return len(self.data)

    def __iter__(self):
        return iter(self.data)

    def __getitem__(self, key):
        events.append(('getitem', key))
        return self.data[key]

    def get(self, key, default=None):
        events.append(('get', key, default is not None))
        return self.data.get(key, default)


events.clear()
match Map():
    case {'a': a, **rest}:
        print('mapping protocol', a, rest, events)
events.clear()
match Map():
    case {'missing': _}:
        print('unexpected mapping')
    case _:
        print('missing key', events)


class RegisteredMap:
    def __len__(self):
        return 1

    def get(self, key, default=None):
        return {'b': 9}.get(key, default)


Mapping.register(RegisteredMap)
match RegisteredMap():
    case {'b': b}:
        print('registered mapping', b)


class Keys:
    key = 'a'


try:
    match {'a': 1, 'b': 2}:
        case {Keys.key: _, 'a': _}:
            pass
except ValueError:
    print('duplicate runtime keys')


class Point:
    __match_args__ = ('x', 'y')

    def __init__(self):
        self.x = 3
        self.y = 4


match Point():
    case Point(x, y=yy):
        print('class', x, yy)


class BadArgs:
    __match_args__ = (1,)


try:
    match BadArgs():
        case BadArgs(x):
            pass
except TypeError:
    print('bad match args')


class Meta(type):
    @property
    def __match_args__(cls):
        events.append('match args')
        raise AttributeError('missing')


class MissingArgs(metaclass=Meta):
    pass


events.clear()
try:
    match MissingArgs():
        case MissingArgs(x):
            pass
except TypeError:
    print('missing match args', events)


class ErrorMeta(type):
    @property
    def __match_args__(cls):
        raise ValueError('descriptor')


class ErrorArgs(metaclass=ErrorMeta):
    pass


try:
    match ErrorArgs():
        case ErrorArgs(x):
            pass
except ValueError as exc:
    print('descriptor error', str(exc))


class Attr:
    @property
    def x(self):
        events.append('attribute')
        raise AttributeError('missing')


events.clear()
match Attr():
    case Attr(x=_):
        print('unexpected attribute')
    case _:
        print('missing attribute', events)


class Ordered:
    @property
    def first(self):
        events.append('first')
        return 1

    @property
    def second(self):
        events.append('second')
        raise ValueError('attribute error')


events.clear()
try:
    match Ordered():
        case Ordered(first=1, second=_):
            pass
except ValueError as exc:
    print('attribute error', str(exc), events)


class IntChild(int):
    pass


for value in (3, IntChild(4), 'text', [1], {'x': 1}):
    match value:
        case int(n):
            print('self int', n)
        case str(s):
            print('self str', s)
        case list(items):
            print('self list', items)
        case dict(items):
            print('self dict', items)
