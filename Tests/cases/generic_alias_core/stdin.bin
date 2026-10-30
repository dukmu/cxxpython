GenericAlias = type(list[int])

a = GenericAlias(list, int)
b = GenericAlias(list, (int,))
print(a, a.__origin__ is list, a.__args__ == (int,), a.__parameters__)
print(a == b, a != GenericAlias(list, str), hash(a) == hash(b))
print(GenericAlias(tuple, ()), GenericAlias(dict, (str, list[int])))
print(a.__mro_entries__((a,)) == (list,))
for operation in (lambda: isinstance([], a), lambda: issubclass(list, a)):
    try:
        operation()
    except TypeError as e:
        print(type(e).__name__, str(e))

class Alias(GenericAlias):
    pass

sub = Alias(list, int)
print(type(sub) is Alias, isinstance(sub, GenericAlias), sub == a)

class Base:
    def __init__(self, value=0):
        self.value = value

class Derived(GenericAlias(Base, int)):
    pass

obj = GenericAlias(Base, int)(value=7)
print(issubclass(Derived, Base), obj.value, obj.__orig_class__ == GenericAlias(Base, int))
print(GenericAlias(list, int)((1, 2)))
