import gc

left = {'a': 1, 'b': 2}
right = {'b': 3, 'c': 4}
print(left | right, left, right)
same = left
left |= [('c', 5), ('a', 6)]
print(left, left is same)
class Mapping:
    def keys(self):
        gc.collect()
        return ['d', 'b']
    def __getitem__(self, key):
        gc.collect()
        return len(key)
left |= Mapping()
print(left)
print(dict.__or__({}, []) is NotImplemented)
print(dict.__ror__({}, []) is NotImplemented)
try:
    {} | [('x', 1)]
except TypeError:
    print('not a dict')
class Derived(dict):
    pass
print(type(Derived(x=1) | {'y': 2}) is dict)
print(type({'x': 1} | Derived(y=2)) is dict)
class Reflected:
    def __ror__(self, other):
        gc.collect()
        return ('reflected', other)
print({'z': 3} | Reflected())
class Failing:
    def __iter__(self):
        yield ('saved', 7)
        raise ValueError('stop')
target = {}
try:
    target |= Failing()
except ValueError as error:
    print(str(error), target)
