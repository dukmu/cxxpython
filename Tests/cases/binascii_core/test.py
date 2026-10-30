import binascii

for value in (b'', b'f', b'fo', b'foo', b'foobar', bytes([0, 127, 128, 255])):
    encoded = binascii.b2a_base64(value, newline=False)
    print(encoded, binascii.a2b_base64(encoded, strict_mode=True))
    print(binascii.hexlify(value), binascii.unhexlify(binascii.hexlify(value)))
print(binascii.b2a_base64(b'abc'))
print(binascii.a2b_base64(' Zm9v\n'))
print(binascii.hexlify(b'abcdef', b':', 2))
print(binascii.hexlify(b'abcdef', '-', -2))
print(binascii.crc32(b'123456789'), binascii.crc_hqx(b'123456789', 0))
print(binascii.crc32(b'', -1), binascii.crc_hqx(b'', -1))
print(binascii.crc32(b'def', binascii.crc32(b'abc')) == binascii.crc32(b'abcdef'))
