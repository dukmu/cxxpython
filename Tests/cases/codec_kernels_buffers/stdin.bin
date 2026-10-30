import _codecs

print(_codecs.utf_8_decode(bytearray(b'abc')))
print(_codecs.utf_16_le_decode(memoryview(b'A\x00')))
print(_codecs.utf_32_ex_decode(memoryview(b'\xff\xfe\x00\x00A\x00\x00\x00')))

def handle(error):
    print(type(error.object).__name__, error.start, error.end)
    return ('?', error.end)

_codecs.register_error('buffer_error', handle)
print(_codecs.ascii_decode(memoryview(b'a\xffz'), 'buffer_error'))
