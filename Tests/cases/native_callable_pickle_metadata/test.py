import _codecs
import gc
import types

print(len.__module__, _codecs.encode.__module__)
print(len.__self__.__name__, _codecs.encode.__self__.__name__)
print(len([1, 2, 3]), _codecs.encode('text', 'ascii'))
print([].append.__module__, object.__new__.__module__)
print(types.MethodType(len, [1]).__qualname__, types.MethodType(len, [1]).__module__)

# Native module metadata is an ordinary writable Python reference, not a name
# inferred from the function's global aliases. Preserve it across collection.
original = len.__module__
try:
    marker = {'metadata': [1, 2, 3]}
    len.__module__ = marker
    del marker
    gc.collect()
    print(len.__module__)
    del len.__module__
    print(len.__module__ is None)
finally:
    len.__module__ = original

values = []
bound = values.append
bound.__module__ = {'bound': True}
gc.collect()
print(bound.__module__, values.append.__module__)
del bound.__module__
print(bound.__module__ is None)

class List(list):
    pass

explicit = types.MethodType(list.append, List())
print(explicit.__qualname__)
try:
    explicit.__module__ = 'not writable'
except AttributeError:
    print('Python method module readonly')

for value in [len, object.__new__, [].append, list.append]:
    try:
        value.__qualname__ = 'not writable'
    except AttributeError:
        print('qualname readonly')
