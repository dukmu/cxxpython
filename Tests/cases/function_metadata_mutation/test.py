def f(x=1, *, y=2):
    "original doc"
    return x, y

code = f.__code__
f.__name__ = 'renamed'
f.__qualname__ = 'scope.renamed'
f.__module__ = 'alternate'
f.__doc__ = 'updated'
f.__type_params__ = (int,)
print(f.__name__, f.__qualname__, f.__module__, f.__doc__, f.__type_params__)
print(vars(f), f.__code__ is code, code.co_name)
print(repr(f).startswith('<function scope.renamed at '))
f.__defaults__ = (3,)
f.__kwdefaults__ = {'y': 4}
print(f())
del f.__defaults__
del f.__kwdefaults__
print(f.__defaults__, f.__kwdefaults__)
try:
    f()
except TypeError:
    print('TypeError')
for name, value in (('__name__', 1), ('__qualname__', None),
                    ('__defaults__', []), ('__kwdefaults__', ()), ('__type_params__', [])):
    try:
        setattr(f, name, value)
    except TypeError:
        print(name, 'TypeError')
f.__doc__ = 5
del f.__module__
print(f.__doc__, f.__module__)
def generator():
    yield 1
generator.__name__ = 'changed'
generator.__qualname__ = 'scope.changed'
g = generator()
print(g.__name__, g.__qualname__)
g.close()
