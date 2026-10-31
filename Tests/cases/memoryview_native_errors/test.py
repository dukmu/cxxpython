import struct

def error(label, action):
    try:
        action()
    except Exception as exc:
        print(label, type(exc).__name__)
    else:
        print(label, 'no error')

for fmt in ('', 'II', '=I', '<I', '>I', 'x', 's', 'e'):
    error(repr(fmt), lambda: memoryview(bytes(8)).cast(fmt))
error('size', lambda: memoryview(bytes(3)).cast('I'))
error('stride', lambda: memoryview(bytes(8))[::2].cast('I'))
error('nonbyte', lambda: memoryview(bytes(8)).cast('I').cast('H'))
view = memoryview(bytearray(8)).cast('I')
for value in (-1, 1 << 32, 1.5, '1'):
    error('range', lambda: view.__setitem__(0, value))
error('readonly', lambda: memoryview(bytes(8)).cast('I').__setitem__(0, 1))
error('format', lambda: view.__setitem__(slice(None), bytes(8)))
error('size mismatch', lambda: view.__setitem__(slice(None), memoryview(bytes(4)).cast('I')))
error('hash native', lambda: hash(memoryview(bytes(8)).cast('I')))
error('hash writable', lambda: hash(view))
error('hash byte owner', lambda: hash(view.toreadonly().cast('B')))
error('float text', lambda: memoryview(bytearray(8)).cast('d').__setitem__(0, '1.0'))
error('char', lambda: memoryview(bytearray(1)).cast('c').__setitem__(0, bytearray(b'a')))
view.release()
error('released', lambda: view.tolist())
print('hash bytes', hash(memoryview(b'abc').cast('@B')) == hash(b'abc'))
print('prefix', memoryview(bytes(8)).cast('@I').format)
