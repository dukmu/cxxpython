import _codecs

table = ''.join(chr(i) for i in range(256))
mapping = _codecs.charmap_build(table)
print(_codecs.charmap_decode(b'A\xff', 'strict', table))
print(_codecs.charmap_encode('A\xff', 'strict', mapping))
print(_codecs.charmap_decode(b'AB', 'strict', {65: 'xyz', 66: 233}))
print(_codecs.charmap_encode('AB', 'strict', {65: b'xy', 66: 90}))
print(_codecs.charmap_decode(b'A\xffB', 'replace', 'AB'))

def fallback(error):
    print(error.encoding, error.start, error.end)
    return ('?', error.end)

_codecs.register_error('charmap_test', fallback)
print(_codecs.charmap_encode('A\u1234B', 'charmap_test', {65: 1, 66: 2, 63: 3}))
print(_codecs.charmap_decode(b'A\xffB', 'charmap_test', {65: 97, 66: 98}))
