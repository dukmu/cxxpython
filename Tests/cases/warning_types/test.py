import builtins

types = (Warning, UserWarning, DeprecationWarning, PendingDeprecationWarning,
         SyntaxWarning, RuntimeWarning, FutureWarning, ImportWarning,
         UnicodeWarning, BytesWarning, ResourceWarning)

for cls in types:
    base = Exception if cls is Warning else Warning
    print(cls.__name__, cls.__module__, getattr(builtins, cls.__name__) is cls,
          cls.__base__ is base, issubclass(cls, Exception))
    instance = cls("message", 42)
    print(type(instance) is cls, isinstance(instance, Warning), instance.args,
          str(instance), repr(instance))
    try:
        raise instance
    except Warning as caught:
        print(caught is instance)

class CustomWarning(UserWarning):
    pass

try:
    raise CustomWarning("custom")
except Warning as caught:
    print(type(caught) is CustomWarning, caught.args, str(caught))
