try:
    reversed(None)
except TypeError as error:
    print(type(error).__name__, str(error))
class IterOnly:
    def __iter__(self):
        return iter([1])
try:
    reversed(IterOnly())
except TypeError as error:
    print(type(error).__name__, str(error))
class Disabled:
    __reversed__ = None
    def __len__(self):
        return 1
    def __getitem__(self, index):
        return index
try:
    reversed(Disabled())
except TypeError as error:
    print(type(error).__name__, str(error))
class BadLength:
    def __len__(self):
        return 1000000000000000000000000
    def __getitem__(self, index):
        return index
try:
    reversed(BadLength())
except OverflowError:
    print('overflow')
class BadItem:
    def __len__(self):
        return 2
    def __getitem__(self, index):
        raise RuntimeError('failed')
it = reversed(BadItem())
try:
    next(it)
except RuntimeError as error:
    print(type(error).__name__, str(error))
print(list(it), it.__length_hint__())
class Short:
    def __len__(self):
        return 2
    def __getitem__(self, index):
        raise IndexError('short')
print(list(reversed(Short())))
