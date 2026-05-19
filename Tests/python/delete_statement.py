value = 'old'
del value
value = 'new'
print(value)

items = ['zero', 'one', 'two']
del items[1]
print(items)

mapping = {'a': 1, 'b': 2}
del mapping['a']
print(mapping)


class Box:
    def __init__(self):
        self.deleted = 'no'

    @property
    def field(self):
        return 'field'

    @field.deleter
    def field(self):
        self.deleted = 'yes'


box = Box()
del box.field
print(box.deleted)


def reset_local():
    marker = 'first'
    del marker
    marker = 'second'
    return marker


print(reset_local())
