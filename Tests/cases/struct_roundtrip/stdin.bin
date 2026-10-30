import struct

for fmt, values in [
    ('>bBhHiIqQ', (-1, 255, -2, 65535, -3, 4294967295, -4, 18446744073709551615)),
    ('<c?4s5p', (b'a', True, b'xy', b'abc')),
    ('>efd', (1.5, -2.25, 3.5)),
]:
    data = struct.pack(fmt, *values)
    print(data.hex())
    print(struct.unpack(fmt, data))

s = struct.Struct('>hB')
print(s.format, s.size, s.pack(-2, 7).hex())
print(s.unpack(b'\xff\xfe\x07'))
b = bytearray(b'........')
s.pack_into(b, 2, -3, 9)
print(bytes(b).hex())
print(s.unpack_from(b, 2))
print(list(struct.iter_unpack('>H', b'\x00\x01\x00\x02')))
print(list(s.iter_unpack(b'\x00\x01\x02\x00\x03\x04')))
for fmt, values in [('B', (256,)), ('b', (-129,)), ('c', (b'xy',)), ('i', ())]:
    try:
        struct.pack(fmt, *values)
    except struct.error:
        print('error')
