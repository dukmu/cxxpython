import gc
import pickle
import weakref

for source in (b'abcd', bytearray(b'abcd')):
    parent = memoryview(source)[1:3]
    buffer = pickle.PickleBuffer(parent)
    raw = buffer.raw()
    assert raw.obj is parent and raw.format == 'B' and raw.tobytes() == b'bc'
    try:
        parent.release()
    except BufferError:
        print('parent pinned', True)
    else:
        raise AssertionError('released an exported parent')
    other = pickle.PickleBuffer(buffer)
    ref = weakref.ref(buffer)
    buffer.release()
    del buffer
    gc.collect()
    assert ref() is None
    with other.raw() as view:
        assert view.tobytes() == b'bc' and view.obj is parent
    other.release()
    try:
        parent.release()
    except BufferError:
        print('raw pins parent', True)
    else:
        raise AssertionError('released a parent with a live raw view')
    raw.release()
    parent.release()

source = bytearray(b'\x01\x00\x00\x00\x02\x00\x00\x00')
parent = memoryview(source).cast('I')
buffer = pickle.PickleBuffer(parent)
view = memoryview(buffer)
raw = buffer.raw()
assert view.obj is parent and view.format == 'I' and view.tolist() == [1, 2]
assert raw.obj is parent and raw.format == 'B' and raw.tobytes() == source
buffer.release()
view.release()
raw.release()
parent.release()
gc.collect()
source.extend(b'x')
print('typed export and release', len(source))

parent = memoryview(b'abcdef')[::2]
buffer = pickle.PickleBuffer(parent)
with memoryview(buffer) as view:
    assert view.strides == (2,) and view.tobytes() == b'ace'
try:
    buffer.raw()
except BufferError:
    print('noncontiguous raw rejected', True)
else:
    raise AssertionError('raw accepted a strided export')
buffer.release()
parent.release()

source = bytearray(b'abc')
buffer = pickle.PickleBuffer(source)
buffer.release()
buffer.release()
source.extend(b'd')
print('immediate lease release', source)
for operation in (buffer.raw, lambda: memoryview(buffer), lambda: pickle.PickleBuffer(buffer)):
    try:
        operation()
    except ValueError:
        print('released export rejected')
    else:
        raise AssertionError('released buffer exported')

try:
    class Derived(pickle.PickleBuffer):
        pass
except TypeError:
    print('subclass rejected')
else:
    raise AssertionError('PickleBuffer accepted subclassing')
