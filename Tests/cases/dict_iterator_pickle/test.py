import copy
import pickle


for kind in ('keys', 'values', 'items'):
    for consumed in (0, 1, 3, 4):
        data = {'a': 10, 'b': 20, 'c': 30}
        iterator = iter(getattr(data, kind)())
        expected = list(getattr(data, kind)())[consumed:]
        for _ in range(consumed):
            next(iterator, None)
        reduction = iterator.__reduce__()
        assert len(reduction) == 2
        assert list(reduction[0](*reduction[1])) == expected
        assert list(copy.copy(iterator)) == expected
        assert list(copy.deepcopy(iterator)) == expected
        for protocol in range(pickle.HIGHEST_PROTOCOL + 1):
            assert list(pickle.loads(pickle.dumps(iterator, protocol))) == expected
        assert list(iterator) == expected
    print(kind, 'cursor round trips')

# This is the nested iterator graph used by dill's dict.values reduction.
iterator = enumerate({'a': 10, 'b': 20, 'c': 30}.values())
assert next(iterator) == (0, 10)
for protocol in range(pickle.HIGHEST_PROTOCOL + 1):
    clone = pickle.loads(pickle.dumps(iterator, protocol))
    assert dict(clone) == {1: 20, 2: 30}
assert list(iterator) == [(1, 20), (2, 30)]
print('enumerate dictionary values')

data = {'a': 1, 'b': 2}
iterator = iter(data.values())
next(iterator)
reduction = iterator.__reduce__()
data['b'] = 99
assert list(reduction[0](*reduction[1])) == [2]
assert list(iterator) == [99]
print('remaining values snapshot')

for kind in ('keys', 'values', 'items'):
    data = {'a': 1, 'b': 2}
    iterator = iter(getattr(data, kind)())
    next(iterator)
    data['extra'] = 3
    try:
        iterator.__reduce__()
    except RuntimeError:
        pass
    else:
        raise AssertionError('changed dictionary accepted')
    del data['extra']
    assert list(iterator) == list(getattr(data, kind)())[1:]
print('failed reduction leaves source valid')


class Example:
    value = 42


iterator = iter(Example.__dict__)
next(iterator)
clone = pickle.loads(pickle.dumps(iterator))
assert list(clone) == list(iterator)
print('type namespace snapshot')
