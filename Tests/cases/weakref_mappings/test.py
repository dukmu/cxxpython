import gc
import weakref

class Item:
    def __init__(self, value):
        self.value = value
    def __repr__(self):
        return 'Item(%r)' % self.value

first, second = Item(1), Item(2)
values = weakref.WeakValueDictionary(a=first, b=second)
print(sorted(values), values['a'] is first)
values['c'] = first
print(sorted(values.items()))
del first
gc.collect()
print(sorted(values), len(values), values.get('a'))
print(values.pop('b') is second, len(values))

first, second = Item(3), Item(4)
keys = weakref.WeakKeyDictionary({first: 'one'})
keys[second] = 'two'
print(keys[first], sorted(keys.values()))
del first
gc.collect()
print(len(keys), keys.get(second), list(keys.keyrefs())[0]() is second)
print(keys.pop(second), len(keys))

class Cycle:
    pass
value = Cycle()
value.link = value
values['cycle'] = value
del value
gc.collect()
print('cycle' in values)
