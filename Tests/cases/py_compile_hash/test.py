import _imp

for key in (0, 168646723, -1):
    for length in (0, 1, 7, 8, 9, 15, 16, 17):
        print(key, length, _imp.source_hash(key, bytes(range(length))).hex())
print(_imp.source_hash(1, bytearray(b'abc')) == _imp.source_hash(1, b'abc'))
try:
    _imp.source_hash(0, 'abc')
except TypeError:
    print('bytes required')
