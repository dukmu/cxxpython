GenericAlias = type(list[int])

class Parameter:
    def __repr__(self):
        return 'T'
    def __typing_subst__(self, value):
        return value

T = Parameter()
alias = GenericAlias(list, T) | int

class Unpacked:
    __typing_unpacked_tuple_args__ = (str,)

print(alias[Unpacked()])

class Variadic(Parameter):
    def __typing_prepare_subst__(self, owner, args):
        if len(args) == 1 and isinstance(args[0], tuple):
            return args
        return (args,)
    def __iter__(self):
        return iter(())

V = Variadic()
class Nested:
    __parameters__ = (V,)
    def __getitem__(self, values):
        print('nested-args', values)
        return GenericAlias(tuple, values)
    def __repr__(self):
        return 'Nested'

variadic = GenericAlias(list, Nested()) | int
print(variadic[str, bytes])

class FailedPrepare(Parameter):
    def __typing_prepare_subst__(self, owner, args):
        raise ValueError('prepare failed')
try:
    (GenericAlias(list, FailedPrepare()) | int)[str]
except ValueError as e:
    print(type(e).__name__, str(e))
