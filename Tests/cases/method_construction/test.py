import gc
from types import MethodType

def add(self, value):
    gc.collect()
    return self + value
method = MethodType(add, 7)
print(method(5), method.__func__ is add, method.__self__)
for function, instance in ((1, object()), (add, None)):
    try:
        MethodType(function, instance)
    except TypeError as error:
        print(type(error).__name__, str(error))
print(int.__new__.__self__ is int)
print(object.__new__.__self__ is object)
print(str.__new__.__self__ is str)
print(None.__doc__, object.__doc__.startswith('The base class'))
