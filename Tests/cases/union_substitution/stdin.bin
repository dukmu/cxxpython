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
alias = GenericAlias(list, T) | GenericAlias(dict, (T, S)) | int
print(alias)
print(alias.__parameters__ == (T, S), alias.__parameters__ is alias.__parameters__)
print(alias[str, float])
print(alias[str, float].__parameters__)
print((GenericAlias(list, T) | GenericAlias(list, S))[int, int] == list[int])
for action in (lambda: alias[str], lambda: alias[str, float, bytes], lambda: (int | str)[float]):
    try:
        action()
    except TypeError as e:
        print(type(e).__name__, str(e))

class Prepared(Parameter):
    def __typing_prepare_subst__(self, owner, args):
        print('prepare', args)
        return (args[0],)
    def __typing_subst__(self, value):
        print('subst', value)
        return value

prepared = GenericAlias(list, Prepared('P')) | int
print(prepared[str, bytes])

class Failed(Parameter):
    def __typing_subst__(self, value):
        raise ValueError('substitution failed')
try:
    (GenericAlias(list, Failed('F')) | int)[str]
except ValueError as e:
    print(type(e).__name__, str(e))
