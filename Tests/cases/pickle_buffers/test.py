import gc
import pickle

source = bytearray(b'buffer payload')
buffer = pickle.PickleBuffer(source)
with buffer.raw() as view:
    assert view.tobytes() == source and not view.readonly
encoded = pickle.dumps(buffer, protocol=5)
decoded = pickle.loads(encoded)
assert isinstance(decoded, bytearray) and decoded == source
print('in band writable', True)

saved = []


def collect_buffer(value):
    gc.collect()
    saved.append(value)
    return False


encoded = pickle.dumps([buffer, pickle.PickleBuffer(b'readonly')], protocol=5,
                       buffer_callback=collect_buffer)
assert len(saved) == 2
views = [value.raw() for value in saved]
result = pickle.loads(encoded, buffers=iter(views))
assert result[0] is views[0] and result[1] is views[1]
assert result[0].tobytes() == source and result[1].readonly
source[0] = ord('B')
assert result[0].tobytes() == b'Buffer payload'
print('out of band alias and readonly', True)
for buffers in (None, []):
    try:
        pickle.loads(encoded, buffers=buffers)
    except pickle.UnpicklingError:
        print('missing buffers', True)
    else:
        raise AssertionError('missing buffers accepted')
try:
    pickle.dumps(buffer, protocol=4)
except pickle.PicklingError:
    print('old protocol rejected', True)
else:
    raise AssertionError('protocol 4 accepted PickleBuffer')
for view in views:
    view.release()
buffer.release()
buffer.release()
try:
    buffer.raw()
except ValueError:
    print('released buffer rejected', True)
else:
    raise AssertionError('released buffer accepted')
