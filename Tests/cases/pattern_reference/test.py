# Adapted from CPython 3.12.14 Lib/test/test_patma.py (PSF license),
# especially TestInheritance and test_patma_184-186, 241-244.
from collections import UserDict, UserList
from collections.abc import Mapping, Sequence
import gc


def sequence_first(value):
    match value:
        case [*_]:
            return 'seq'
        case {}:
            return 'map'


def mapping_first(value):
    match value:
        case {}:
            return 'map'
        case [*_]:
            return 'seq'


class MappingFirst(UserDict, Sequence):
    pass


class MappingBuiltin(dict, Sequence):
    pass


class SequenceFirst(UserList, Mapping):
    pass


class SequenceBuiltin(list, Mapping):
    pass


for value, expected in ((MappingFirst(), 'map'), (MappingBuiltin(), 'map'),
                        (SequenceFirst(), 'seq'), (SequenceBuiltin(), 'seq')):
    assert sequence_first(value) == expected
    assert mapping_first(value) == expected


class MapParent:
    pass


class MapPre(MapParent):
    pass


class MapGrandPre(MapPre):
    pass


Mapping.register(MapParent)


class MapPost(MapParent):
    pass


class MapGrandPost(MapPost):
    pass


class SeqParent:
    pass


class SeqPre(SeqParent):
    pass


class SeqGrandPre(SeqPre):
    pass


Sequence.register(SeqParent)


class SeqPost(SeqParent):
    pass


class SeqGrandPost(SeqPost):
    pass


for cls in (MapParent, MapPre, MapGrandPre, MapPost, MapGrandPost):
    assert sequence_first(cls()) == 'map'
    assert mapping_first(cls()) == 'map'
for cls in (SeqParent, SeqPre, SeqGrandPre, SeqPost, SeqGrandPost):
    assert sequence_first(cls()) == 'seq'
    assert mapping_first(cls()) == 'seq'


events = []


class Probe(Sequence):
    def __len__(self):
        events.append('len')
        gc.collect()
        return 4

    def __getitem__(self, index):
        events.append(index)
        gc.collect()
        if index >= 4:
            raise IndexError
        return index


match Probe():
    case [*_]:
        pass
    case _:
        assert False
assert events == []
match Probe():
    case [_, *_]:
        pass
    case _:
        assert False
assert events == ['len']
events.clear()
match Probe():
    case [first, *_, last]:
        assert (first, last) == (0, 3)
    case _:
        assert False
assert events == ['len', 0, 'len', 3]
events.clear()
match Probe():
    case [*_, last]:
        assert last == 3
    case _:
        assert False
assert events == ['len', 'len', 3]


def singleton(value):
    match value:
        case True:
            return 'true'
        case False:
            return 'false'
        case None:
            return 'none'
        case 1:
            return 'one'
        case 0:
            return 'zero'
        case _:
            return 'other'


assert [singleton(x) for x in (True, False, None, 1, 0, 1.0, 0j)] == [
    'true', 'false', 'none', 'one', 'zero', 'one', 'zero']


class Values:
    number = -2 + 3j
    text = 'hello'


def literal(value):
    match value:
        case -2 + 3j:
            return 'complex'
        case -4:
            return 'negative'
        case 2.5:
            return 'float'
        case Values.text:
            return 'dotted'
        case 'a' 'b':
            return 'string'
        case b'a' b'b':
            return 'bytes'
        case _:
            return 'other'


assert [literal(x) for x in (Values.number, -4.0, 2.5, 'hello', 'ab', b'ab')] == [
    'complex', 'negative', 'float', 'dotted', 'string', 'bytes']


match (1, 2):
    case (1, 2):
        pass
    case _:
        assert False
match ():
    case ():
        pass
    case _:
        assert False
match 1:
    case (1):
        pass
    case _:
        assert False
match [1, 2]:
    case first, second:
        assert (first, second) == (1, 2)
    case _:
        assert False
for value in ('ab', b'ab', bytearray(b'ab')):
    match value:
        case [_, _]:
            assert False
        case _:
            pass


class Name(str):
    pass


class Point:
    __match_args__ = (Name('x'),)
    x = 42


try:
    match Point():
        case Point(x):
            assert False
except TypeError:
    pass
else:
    assert False

Point.__match_args__ = ('x',)


class Pretend:
    @property
    def __class__(self):
        gc.collect()
        return Point

    x = 17


match Pretend():
    case Point(x):
        assert x == 17
    case _:
        assert False


class Meta(type):
    def __instancecheck__(cls, instance):
        gc.collect()
        return instance == 'accepted'


class Checked(metaclass=Meta):
    pass


match 'accepted':
    case Checked():
        pass
    case _:
        assert False
match 'rejected':
    case Checked():
        assert False
    case _:
        pass

print('pattern reference: ok')
