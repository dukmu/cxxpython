import pickle


class Index:
    def __index__(self):
        return 1


for sequence in ([1, 2, 3], (1, 2, 3), 'abc'):
    for state in (-5, 0, 1, True, 100):
        iterator = iter(sequence)
        assert iterator.__setstate__(state) is None
        print(type(sequence).__name__, state, list(iterator))
    for state in ('1', 1.5, Index(), 2 ** 100, -(2 ** 100)):
        iterator = iter(sequence)
        try:
            iterator.__setstate__(state)
        except (TypeError, OverflowError) as exc:
            print(type(exc).__name__)
        else:
            raise AssertionError('invalid iterator state accepted')
        assert list(iterator) == list(sequence)
    iterator = iter(sequence)
    iterator.__setstate__(100)
    iterator.__setstate__(1)
    assert list(iterator) == list(sequence)[1:]
    iterator.__setstate__(0)
    assert list(iterator) == []
    try:
        iterator.__setstate__(2 ** 100)
    except OverflowError:
        print('exhausted state validated')
    else:
        raise AssertionError('exhausted iterator accepted overflowing state')


class Indexed:
    def __init__(self):
        self.values = [1, 2, 3]

    def __getitem__(self, index):
        return self.values[index]


source = Indexed()
iterator = iter(source)
iterator.__setstate__(4)
source.values.extend([4, 5])
assert next(iterator) == 5
iterator.__setstate__(1)
clone = pickle.loads(pickle.dumps(iterator))
print(list(clone), list(iterator))
