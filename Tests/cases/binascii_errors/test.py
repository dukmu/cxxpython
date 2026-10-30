import binascii

def check(fn, *args, **kwargs):
    try:
        fn(*args, **kwargs)
    except Exception as e:
        print(type(e).__name__, str(e))

for data in (b'=', b'!AAA', b'AA=A', b'AA==x', b'A', b'AA', b'abcde'):
    check(binascii.a2b_base64, data, strict_mode=True)
check(binascii.unhexlify, b'0')
check(binascii.unhexlify, b'xx')
check(binascii.unhexlify, '\u00e9')
print(issubclass(binascii.Error, ValueError))
