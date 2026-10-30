print(list(reversed([1, 2, 3])))
print(list(reversed((1, 2, 3))))
print(list(reversed('abc')))
print(list(reversed(b'abc')))
print(list(reversed(bytearray(b'abc'))))
class Sequence:
    def __len__(self):
        return 3
    def __getitem__(self, index):
        print('get', index)
        return index + 10
print(list(reversed(Sequence())))
class Custom:
    def __reversed__(self):
        print('custom')
        return iter([7, 8])
print(list(reversed(Custom())))
class ListChild(list):
    def __getitem__(self, index):
        print('override')
        return 99
print(list(reversed(ListChild([1, 2, 3]))))
items = [1, 2, 3]
it = reversed(items)
print(it.__length_hint__(), next(it), it.__length_hint__())
items.clear()
print(it.__length_hint__(), list(it))
items.append(9)
print(list(it), it.__length_hint__())
