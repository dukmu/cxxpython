import io
import math
import pickle

# Public policy expectations are pinned to CPython 3.12.14.
values = [None, True, False, 0, -257, 2 ** 130, -(2 ** 130), 1.25,
          float('inf'), float('-inf'), 'line\n\x00\u4e2d\ud800',
          b'\x00\xff\n', bytearray(b'abc'), (), (1, 'two'),
          [1, 2], {'first': 1, 'second': 2}, {3, 4}, frozenset({5, 6})]
print('protocol limits', pickle.DEFAULT_PROTOCOL, pickle.HIGHEST_PROTOCOL)
for protocol in (3, 4, 5, 1, 2, 0):
    encoded = pickle.dumps(values, protocol=protocol)
    decoded = pickle.loads(encoded)
    assert decoded == values
    assert [type(value) for value in decoded] == [type(value) for value in values]
    if protocol >= 2:
        assert encoded[:2] == bytes((128, protocol))
    stream = io.BytesIO()
    pickle.dump(values, stream, protocol=protocol)
    stream.seek(0)
    assert pickle.load(stream) == values
    assert math.isnan(pickle.loads(pickle.dumps(float('nan'), protocol=protocol)))
    assert math.copysign(1, pickle.loads(pickle.dumps(-0.0, protocol=protocol))) == -1
    print('roundtrip', protocol, len(decoded), decoded[10] == values[10])
assert pickle.dumps(None) == pickle.dumps(None, protocol=4)
assert pickle.dumps(None, protocol=-1) == pickle.dumps(None, protocol=5)
print('protocol defaults', True)
