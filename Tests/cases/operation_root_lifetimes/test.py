import gc


class HashValue:
    def __init__(self, value):
        self.value = value

    def __hash__(self):
        gc.collect()
        return self.value


class BadHash:
    def __hash__(self):
        gc.collect()
        raise ValueError('hash error')


print(hash(HashValue(-1)), hash(HashValue(2**100)))
try:
    hash(BadHash())
except ValueError as error:
    print(type(error).__name__, str(error))


class Answer:
    def __bool__(self):
        gc.collect()
        return True


class LengthAnswer:
    def __len__(self):
        gc.collect()
        return 1


class Left:
    def __eq__(self, other):
        gc.collect()
        return NotImplemented


class Right:
    def __eq__(self, other):
        gc.collect()
        return Answer()


print(bool(Left() == Right()), bool(LengthAnswer()))


class Key:
    def __init__(self, name):
        self.name = name

    def __hash__(self):
        gc.collect()
        return 17

    def __eq__(self, other):
        gc.collect()
        if action:
            operation = action.pop()
            if operation == 'clear':
                data.clear()
            elif operation == 'replace':
                del data[self]
                data[self] = 'reinserted'
            else:
                data['side'] = 'added'
        return LengthAnswer()


data = {}
action = []
key = Key('stored')
data[key] = 'original'
action.append('side')
print(data.get(Key('temporary')), data['side'])
action.append('replace')
print(data.setdefault(Key('temporary'), 'default'), data[key])
action.append('clear')
print(data.pop(Key('temporary'), 'missing'), len(data))
print(data.setdefault(Key('new'), 'new value'), data.pop(Key('new')))


class Index:
    def __init__(self, value):
        self.value = value

    def __index__(self):
        gc.collect()
        return self.value


items = ['a', 'b', 'c']
print(items[Index(1)])
items[Index(1)] = 'changed'
del items[Index(0)]
print(items, ['x'] * Index(2))


class BoolResult:
    def __init__(self, value):
        self.value = value

    def __bool__(self):
        gc.collect()
        return self.value


class LenResult:
    def __init__(self, value):
        self.value = value

    def __len__(self):
        gc.collect()
        return int(self.value)


class Comparable:
    def __init__(self, value):
        self.value = value

    def __eq__(self, other):
        gc.collect()
        return LenResult(self.value == other.value)

    def __lt__(self, other):
        gc.collect()
        return BoolResult(self.value < other.value)


values = [Comparable(3), Comparable(1), Comparable(2)]
print(Comparable(1) in values, values.index(Comparable(2)),
      values.count(Comparable(3)))
values.sort()
print([value.value for value in values])
print([Comparable(1), Comparable(2)] == [Comparable(1), Comparable(2)],
      bool([Comparable(1)] < [Comparable(2)]))


class SetKey:
    def __init__(self, value):
        self.value = value

    def __hash__(self):
        gc.collect()
        return 23

    def __eq__(self, other):
        gc.collect()
        return LenResult(self.value == other.value)


keys = {SetKey(1), SetKey(2)}
print(SetKey(1) in keys, SetKey(3) in keys)
keys.discard(SetKey(1))
keys.add(SetKey(3))
print(len(keys), SetKey(1) in keys, SetKey(2) in keys, SetKey(3) in keys)
