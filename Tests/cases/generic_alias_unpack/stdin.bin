GenericAlias = type(list[int])

a = GenericAlias(tuple, (int, str))
star = next(iter(a))
print(a.__unpacked__, star.__unpacked__, star.__typing_unpacked_tuple_args__)
print(star, star == a, GenericAlias(list, int).__name__)
print(type(iter(a)).__name__, a.__reduce__() == (GenericAlias, (tuple, (int, str))))
it = iter(a)
print(next(it))
try:
    next(it)
except StopIteration:
    print('exhausted')

class Parameter:
    def __repr__(self):
        return 'P'
    def __typing_subst__(self, value):
        return value

P = Parameter()
print(GenericAlias(tuple, (P,))[next(iter(tuple[int]))])

class Variadic:
    __typing_is_unpacked_typevartuple__ = True
    def __repr__(self):
        return 'Ts'
    def __typing_prepare_subst__(self, alias, args):
        return (args,)
    def __typing_subst__(self, value):
        return value

print(GenericAlias(tuple, Variadic())[int, str])
