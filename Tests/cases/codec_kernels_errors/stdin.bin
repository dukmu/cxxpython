import _codecs

print(_codecs.ascii_encode('a\xe9z', 'xmlcharrefreplace'))
print(_codecs.utf_16_le_encode('a\ud800z', 'backslashreplace'))
print(_codecs.utf_32_be_decode(b'\x00\x11\x00\x00', 'replace', True))
print(_codecs.utf_8_decode(b'a\xffz', 'surrogateescape', True))
print(_codecs.utf_8_encode('\udcff', 'surrogateescape'))

def handler(error):
    print(type(error).__name__, error.start, error.end, error.reason)
    return ('!', error.end)

_codecs.register_error('kernel_test', handler)
print(_codecs.ascii_encode('a\xe9z', 'kernel_test'))
print(_codecs.utf_16_le_decode(b'A\x00\x00\xdc', 'kernel_test', True))
