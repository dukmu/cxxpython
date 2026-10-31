import gc
import pickle
import re
import weakref

data = bytearray(b'_ab cd ef_')
view = memoryview(data)[1:-1]
reference = weakref.ref(view)
source = pickle.PickleBuffer(view)
scanner = re.compile(rb'[a-z]+').scanner(source)
first = scanner.search()
assert first.group() == b'ab' and first.string is source
print('initial', first.span(), first.group())
source.release()
del view, source, first
gc.collect()
assert reference() is not None
data[4:6] = b'XY'
second = scanner.search()
assert second.span() == (6, 8)
print('released subject refresh', second.span(), scanner.search() is None)
try:
    second.group()
except TypeError:
    print('group reexports original', True)
else:
    raise AssertionError('released subject group accepted')
for label, function in (('resize', lambda: data.append(33)),
                        ('exporter release', lambda: reference().release())):
    try:
        function()
    except BufferError:
        print('scanner blocks', label)
    else:
        raise AssertionError(label)
del scanner, second
gc.collect()
assert reference() is None
data.append(33)
print('scanner releases exporter', bytes(data))

view = memoryview(b'one two')
reference = weakref.ref(view)
source = pickle.PickleBuffer(view)
iterator = re.finditer(rb'[a-z]+', source)
source.release()
del source, view
gc.collect()
assert reference() is not None
print('immutable owner spans', [match.span() for match in iterator])
del iterator
gc.collect()
print('immutable exporter collected', reference() is None)
