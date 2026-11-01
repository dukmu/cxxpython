import copy
import pickle


for sequence in ([10, 20, 30], (10, 20, 30), 'a\u03bbc'):
    for consumed in (0, 1, 3, 4):
        iterator = iter(sequence)
        for _ in range(consumed):
            next(iterator, None)
        expected = list(sequence)[consumed:]
        reduction = iterator.__reduce__()
        clone = reduction[0](*reduction[1])
        if len(reduction) == 3:
            clone.__setstate__(reduction[2])
        assert list(clone) == expected
        assert list(copy.copy(iterator)) == expected
        assert list(copy.deepcopy(iterator)) == expected
        for protocol in range(pickle.HIGHEST_PROTOCOL + 1):
            assert list(pickle.loads(pickle.dumps(iterator, protocol))) == expected
        assert list(iterator) == expected
    print(type(sequence).__name__, 'cursor round trips')

for start in (-5, 0, 2 ** 100):
    for sequence in ([10, 20, 30], (10, 20, 30), 'abc'):
        iterator = enumerate(sequence, start)
        assert next(iterator) == (start, sequence[0])
        reduction = iterator.__reduce__()
        assert reduction[0] is enumerate and reduction[1][1] == start + 1
        expected = [(start + 1, sequence[1]), (start + 2, sequence[2])]
        for protocol in range(pickle.HIGHEST_PROTOCOL + 1):
            assert list(pickle.loads(pickle.dumps(iterator, protocol))) == expected
        assert list(copy.deepcopy(iterator)) == expected
        assert list(iterator) == expected
print('enumerate arbitrary indexes')


class Numbered(enumerate):
    pass


iterator = Numbered(['x', 'y'], 10)
next(iterator)
clone = pickle.loads(pickle.dumps(iterator))
assert type(clone) is Numbered
assert list(clone) == [(11, 'y')]
assert list(iterator) == [(11, 'y')]
print('enumerate subclass')

data = [1, 2]
iterator = iter(data)
next(iterator)
reduction = iterator.__reduce__()
assert reduction[1][0] is data
data.append(3)
clone = reduction[0](*reduction[1])
clone.__setstate__(reduction[2])
print(list(clone), list(iterator))
