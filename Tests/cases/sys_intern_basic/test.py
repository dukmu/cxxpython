import sys
import gc

first = ''.join(['intern ', 'dynamic ', 'value'])
second = ''.join(['intern ', 'dynamic ', 'value'])
a = sys.intern(first)
b = sys.intern(second)
print(a == first, a is b, sys.intern(a) is a)
gc.collect()
print(sys.intern(''.join(['intern ', 'dynamic ', 'value'])) is a)
class Text(str):
    pass
for value in (123, Text('value')):
    try:
        sys.intern(value)
    except TypeError as exc:
        print(type(exc).__name__, str(exc))
try:
    sys.intern(string='x')
except TypeError:
    print('positional only')
