import gc
import pickle
import re

pattern = re.compile(rb'.')
view = memoryview(bytearray(b'abcdef'))[::2]
source = pickle.PickleBuffer(view)
for label, function in (
    ('strided view', lambda: pattern.search(view)),
    ('strided pickle buffer', lambda: pattern.scanner(source)),
    ('strided substitution', lambda: pattern.sub(b'X', source)),
    ('empty strided view', lambda: pattern.search(memoryview(b'ab')[0:0:2])),
):
    try:
        function()
    except TypeError as exc:
        print(label, type(exc).__name__, str(exc))
    else:
        raise AssertionError(label)
source.release()
try:
    pattern.search(source)
except TypeError as exc:
    print('released', type(exc).__name__, str(exc))
else:
    raise AssertionError('released buffer accepted')
source = pickle.PickleBuffer(b'abcd')
try:
    re.search('a', source)
except TypeError as exc:
    print('pattern kind', type(exc).__name__, str(exc))
else:
    raise AssertionError('text pattern accepted bytes subject')
source.release()

data = bytearray(b'abcd')
view = memoryview(data).cast('H')
source = pickle.PickleBuffer(view)
assert re.findall(rb'.', source) == [b'a', b'b', b'c', b'd']
print('typed contiguous byte length', True)
source.release()
del view, source
gc.collect()
data.append(33)

for view in (memoryview(b'ab')[1:0:-1], memoryview(b'ab')[0:0]):
    source = pickle.PickleBuffer(view)
    match = re.fullmatch(rb'b' if len(view) else rb'', source)
    assert match is not None
    print('singleton or empty', match.span())
    source.release()
    del match, source
