import binascii

for data in (b'', b'a', b'ab', b'abc', b'\x00\x00\xff', b'x' * 45):
    for backtick in (False, True):
        encoded = binascii.b2a_uu(data, backtick=backtick)
        print(encoded, binascii.a2b_uu(encoded) == data)
for data in (b'', b'!', b'!\n', b'!@', b'!@!', b' ` \r\n', b'!@bad', b'!\x00'):
    try:
        print(data, binascii.a2b_uu(data))
    except binascii.Error as e:
        print(data, type(e).__name__, str(e))
try:
    binascii.b2a_uu(b'x' * 46)
except binascii.Error as e:
    print(type(e).__name__, str(e))
print(issubclass(binascii.Incomplete, Exception), issubclass(binascii.Incomplete, binascii.Error))
print(binascii.Incomplete.__module__, binascii.Incomplete.__name__)
