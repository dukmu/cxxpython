GenericAlias = type(list[int])

class Reject:
    def __setattr__(self, name, value):
        if name == '__orig_class__':
            raise ValueError('rejected')
        object.__setattr__(self, name, value)

class Ignore:
    def __setattr__(self, name, value):
        if name == '__orig_class__':
            raise TypeError('ignored')
        object.__setattr__(self, name, value)

try:
    GenericAlias(Reject, int)()
except ValueError as e:
    print(type(e).__name__, str(e))
print(type(GenericAlias(Ignore, int)()).__name__)
