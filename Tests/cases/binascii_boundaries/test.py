import binascii

def check(fn, *args, **kwargs):
    try:
        print(fn(*args, **kwargs))
    except Exception as e:
        print(type(e).__name__, str(e))

def check_type(fn, *args, **kwargs):
    try:
        fn(*args, **kwargs)
    except Exception as e:
        print(type(e).__name__)

check(binascii.a2b_uu, 7)
check(binascii.a2b_qp, '\u00e9')
check(binascii.b2a_qp, 'abc')
check(binascii.hexlify, b'abc', b':', 0)
check(binascii.hexlify, b'abc', b':', -2)
check(binascii.hexlify, b'abc', b'')
check(binascii.hexlify, b'abc', bytearray(b':'))
check(binascii.hexlify, b'abc', b':', 2 ** 40)
check(binascii.crc32, b'abc', -1)
check(binascii.crc32, b'abc', 2 ** 70 + 3)
check(binascii.crc_hqx, b'', -1)
check(binascii.a2b_base64, b'AA==junk')
check(binascii.a2b_base64, b'AA==junk', strict_mode=True)
print(binascii.hexlify(bytearray(b'abc')))
print(binascii.a2b_qp(memoryview(b'a=20b')))
check(binascii.a2b_uu, memoryview(b'abcdef')[::2])
check(binascii.b2a_qp, memoryview(b'abcdef')[::2])
check_type(binascii.b2a_uu, b'abc', True)
check_type(binascii.a2b_base64, b'AA==', True)
check_type(binascii.a2b_qp, b'a', False, False)
check_type(binascii.b2a_qp, b'a', False, True, False, True)
check_type(binascii.crc32, b'abc', crc=1)
check(binascii.hexlify, b'abc', binascii)
check(binascii.hexlify, b'abc', b':', -(2 ** 80))
check(binascii.crc32, b'abc', -(2 ** 80) - 3)
check(binascii.crc_hqx, b'abc', 2 ** 80 + 65535)
