# Index and reflected numeric callbacks run before sequence repetition.
import gc

class Index:
    def __init__(self, owner):
        self.owner = owner
        self.calls = 0
    def __index__(self):
        self.calls += 1
        self.owner.clear()
        gc.collect()
        return 3

owner = {'text': 'temporary-' + chr(945)}
count = Index(owner)
print(owner['text'] * count, count.calls, owner)
owner = {'text': chr(946) + '-temporary'}
count = Index(owner)
print(count * owner['text'], count.calls, owner)

class IntSubclass(int):
    def __index__(self):
        raise RuntimeError('must not call an int subclass index override')

print('a' * IntSubclass(2), IntSubclass(2) * 'b')

class Reflected:
    def __rmul__(self, other):
        return ('reflected', other)
    def __index__(self):
        raise RuntimeError('numeric reflection must precede repetition')

print('abc' * Reflected())

class Fallback:
    def __rmul__(self, other):
        return NotImplemented
    def __index__(self):
        return 2

print('ab' * Fallback())

class HugeIndex:
    def __index__(self):
        gc.collect()
        return 10**100

try:
    'a' * HugeIndex()
except OverflowError as error:
    print(type(error).__name__, str(error))
