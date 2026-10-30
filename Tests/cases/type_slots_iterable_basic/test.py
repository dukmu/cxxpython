class Sequence:
    def __getitem__(self, index):
        if index == 0:
            return 'obj'
        raise IndexError

def names():
    yield 'obj'

for slots in (['obj'], ('obj',), {'obj': 'documentation'}, {'obj'},
              iter(['obj']), names(), Sequence(), 'obj'):
    cls = type('Slotted', (), {'__slots__': slots})
    value = cls()
    value.obj = 42
    print(value.obj, cls.__slots__ is slots, hasattr(value, '__dict__'))
    del value.obj
    print(hasattr(value, 'obj'))

class Empty:
    __slots__ = iter(())
print(hasattr(Empty(), '__dict__'))

class Private:
    __slots__ = ['__value']
    def set(self, value):
        self.__value = value
private = Private()
private.set(7)
print(private._Private__value)

class SlotName(str):
    def __iter__(self):
        raise AssertionError('a string slot must not be iterated')
class StringSlot:
    __slots__ = SlotName('obj')
value = StringSlot()
value.obj = 9
print(value.obj)

class ListSlots(list):
    def __iter__(self):
        yield 'obj'
class TupleSlots(tuple):
    def __iter__(self):
        yield 'obj'
class DictSlots(dict):
    def __iter__(self):
        yield 'obj'
for slots in (ListSlots(['ignored']), TupleSlots(('ignored',)), DictSlots(ignored=1)):
    cls = type('Overridden', (), {'__slots__': slots})
    value = cls()
    value.obj = 11
    print(value.obj, hasattr(cls, 'ignored'))
