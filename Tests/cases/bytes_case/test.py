for family in (bytes, bytearray):
    for raw in (b'', b'123\x00', b'AbZaz \xc0\xff', bytes(range(256))):
        original = family(raw)
        print(type(original.upper()).__name__, original.upper(), original.lower())
        print(original == family(raw))

class Bytes(bytes):
    pass

class ByteArray(bytearray):
    pass

for original in (Bytes(b'Ab'), ByteArray(b'Ab')):
    print(type(original.upper()).__name__, type(original.lower()).__name__)
for operation in (lambda: bytes.upper('text'), lambda: bytearray.lower(b'bytes'),
                  lambda: b'x'.upper(1), lambda: b'x'.lower(value=1)):
    try:
        operation()
    except TypeError as exc:
        print(type(exc).__name__)
