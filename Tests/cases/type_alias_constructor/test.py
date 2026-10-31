from _typing import TypeAliasType

alias = TypeAliasType('Alias', int)
other = TypeAliasType('Alias', int)
print(TypeAliasType.__module__, repr(TypeAliasType))
print(TypeAliasType(name='Keyword', value=str).__value__ is str)
print(alias.__name__, alias.__value__ is int, alias.__type_params__, alias.__module__)
print(alias is other, alias == other, hash(alias) == hash(alias))
for name in ('__name__', '__value__', '__type_params__', '__module__'):
    try:
        setattr(alias, name, None)
    except (AttributeError, TypeError) as error:
        print(name, type(error).__name__)
try:
    import _weakref
    _weakref.ref(alias)
except TypeError as error:
    print('weakref', type(error).__name__)
try:
    class Child(TypeAliasType):
        pass
except TypeError as error:
    print('subclass', type(error).__name__)
for params in ([], None, 1):
    try:
        TypeAliasType('Bad', int, type_params=params)
    except TypeError as error:
        print('params', type(error).__name__, str(error))
try:
    alias[int]
except TypeError as error:
    print('subscript', type(error).__name__, str(error))
generic = TypeAliasType('Generic', list, type_params=(int,))
applied = generic[str]
print(generic.__type_params__, applied.__origin__ is generic, applied.__args__)
union = generic | int
print(union.__args__[0] is generic, union.__args__[1] is int)
error = NameError('message', name='symbol')
print(error.name, error.args)
del error.name
print(error.name)
