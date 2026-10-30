GenericAlias = type(list[int])

class Parameter:
    def __init__(self, name):
        self.name = name
    def __repr__(self):
        return self.name
    def __typing_subst__(self, value):
        return value

T = Parameter('T')
S = Parameter('S')
a = GenericAlias(dict, (T, GenericAlias(list, S)))
print(a, a.__parameters__ == (T, S))
print(a[int, str], a[int, str].__parameters__)
print(GenericAlias(tuple, (T, T)).__parameters__ == (T,))
for fn in (lambda: GenericAlias(list, int)[str], lambda: a[int], lambda: a[int, str, float]):
    try:
        fn()
    except TypeError as e:
        print(type(e).__name__, str(e))

class Prepared(Parameter):
    def __typing_prepare_subst__(self, alias, args):
        print('prepare', args)
        return (args[0],)
    def __typing_subst__(self, value):
        print('subst', value)
        return value

print(GenericAlias(list, Prepared('P'))[int, str])
