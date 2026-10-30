import _codecs

def replace_object(error):
    error.object = b'xyz'
    return ('!', -1)

def replace_bytes(error):
    return (b'\xff', error.end)

def invalid_position(error):
    return ('?', 100)

_codecs.register_error('kernel_object', replace_object)
_codecs.register_error('kernel_bytes', replace_bytes)
_codecs.register_error('kernel_position', invalid_position)
print(_codecs.ascii_decode(b'a\xff', 'kernel_object'))
print(_codecs.ascii_encode('a\xe9z', 'kernel_bytes'))
try:
    _codecs.ascii_encode('\xe9', 'kernel_position')
except IndexError:
    print('position out of bounds')
