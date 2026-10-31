# CPython 3.12.14 Python/bltinmodule.c filter_new/filter_next/filter_reduce;
# PSF license: doc/licenses/python-3.12.14.txt.

print(filter.__name__, filter.__module__)
for predicate in (None, bool):
    print(list(filter(predicate, [0, 1, '', 'word', [], [3], None, False, True])))
print(list(filter(lambda item: item > 0, [1, -3, 9, 0, 2])))
print(''.join(filter(lambda char: 'a' <= char <= 'z', 'Hello World')))
print(list(filter(None, b'\x00\x01\x00\xff')))
print(list(filter(None, {'': 0, 'first': 1, 'second': 0})))

events = []


class Source:
    def __iter__(self):
        events.append('iter')
        return self

    def __next__(self):
        events.append('next')
        return len(events) - 1


def odd(item):
    events.append(('predicate', item))
    return item % 2


value = filter(odd, Source())
print('constructed', events)
print('identity', iter(value) is value)
print('first', next(value), events)
print('second', next(value), events)

source = iter(range(6))
value = filter(lambda item: item % 2, source)
print('shared', next(source), next(value), next(source), list(value))


class Sequence:
    def __getitem__(self, index):
        if index == 5:
            raise IndexError
        return index


print('getitem', list(filter(None, Sequence())))


class Truth:
    def __init__(self, result):
        self.result = result

    def __bool__(self):
        events.append(('truth', self.result))
        return self.result


for predicate in (None, bool, lambda item: item):
    events.clear()
    false_item, true_item = Truth(False), Truth(True)
    result = list(filter(predicate, [false_item, true_item]))
    print('truth once', len(result), result[0] is true_item, events)


class Length:
    def __init__(self, size):
        self.size = size

    def __len__(self):
        return self.size


print('length truth', len(list(filter(None, [Length(0), Length(2)]))))
left = filter(None, [])
right = filter(None, [])
print('object protocols', left == left, left == right, hash(left) == hash(left), bool(left))
print('repr', repr(left).startswith('<filter object at '), str(left) == repr(left))
print('empty', next(left, 'done'), next(left, 'done'))
print('long rejection', next(filter(None, [0] * 20000 + [7])))
