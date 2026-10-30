import binascii

for data in (b'', b'hello world', b'a=b_\xff', b'a \t\n.\n', b'a\r\nb\n', b'.', b'x' * 80, b'x' * 74 + b'\xff'):
    for quote, text, header in ((False, True, False), (True, True, False), (False, False, True)):
        encoded = binascii.b2a_qp(data, quote, text, header)
        print(encoded, binascii.a2b_qp(encoded, header=header))
for data in (b'a=20b', b'a=\nb', b'a=\rskip\nb', b'a==b', b'a=xy', b'a=', b'a_b', b'=ff'):
    print(data, binascii.a2b_qp(data), binascii.a2b_qp(data, True))
print(binascii.b2a_qp(data=b'a b_', header=True))
print(binascii.a2b_qp(data='a_b=3D', header=True))
