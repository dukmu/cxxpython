import marshal

values = (
    None, False, True, Ellipsis, StopIteration,
    0, -1, 2147483647, -2147483648, 2 ** 200, -(2 ** 200),
    1.25, -0.0, 2.5 + 3.75j,
    '', 'ascii', '中文', '\ud800', b'', b'\x00\xff',
    (), (1, 'x'), [], [1, 'x'], {}, {'a': 1},
    set(), {1, 2}, frozenset(), frozenset((1, 2)),
)
print(marshal.version)
for value in values:
    decoded = marshal.loads(marshal.dumps(value))
    print(type(value).__name__, decoded == value)

# Explicit records from the format, including older encodings accepted by v4.
records = (
    b'i\xff\xff\xff\xff',
    b'I\x00\x00\x00\x00\x01\x00\x00\x00',
    b'l\x02\x00\x00\x00\x01\x00\x01\x00',
    b'f\x031.5',
    b'x\x031.5\x042.25',
    b'u\x03\x00\x00\x00\xe4\xb8\xad',
    b'a\x03\x00\x00\x00abc',
    b'z\x03abc',
    b')\x02i\x01\x00\x00\x00N',
)
for record in records:
    print(repr(marshal.loads(record)))

class BytesSubclass(bytes):
    pass

for value in (BytesSubclass(b'abc'), bytearray(b'abc'), memoryview(b'abc')):
    decoded = marshal.loads(marshal.dumps(value))
    print(type(decoded).__name__, decoded == b'abc')
