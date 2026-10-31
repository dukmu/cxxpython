import codecs
import _codecs

encode = codecs.raw_unicode_escape_encode
decode = codecs.raw_unicode_escape_decode
assert encode is _codecs.raw_unicode_escape_encode
assert decode is _codecs.raw_unicode_escape_decode
print('empty', encode(''), decode(b''))
latin = ''.join(chr(i) for i in range(256))
assert encode(latin) == (bytes(range(256)), 256)
for i in range(256):
    assert decode(bytes((i,)) + b'0') == (chr(i) + '0', 2)
print('latin1 literal', True)
text = '\u0100\u20ac\ud800\udfff\uffff\U00010000\U0010ffff'
raw, consumed = encode(text)
assert consumed == len(text) and decode(raw) == (text, len(raw))
print('wide and surrogates', raw)
for raw in (br'\n\t\x41\123\N{SPACE}', b'\\', br'\\u0041', br'\\\u0041'):
    print('backslashes', raw, repr(decode(raw)[0]))
for name in ('raw-unicode-escape', 'raw_unicode_escape', 'RAW Unicode Escape',
             ' raw---unicode---escape '):
    info = codecs.lookup(name)
    assert info.name == 'raw-unicode-escape'
    assert text.encode(name) == encode(text)[0]
    assert encode(text)[0].decode(name) == text
    print('alias', name, info.name)
for value in (b'\xff\\u20ac', bytearray(b'\xff\\u20ac'), memoryview(b'\xff\\u20ac')):
    assert decode(value) == ('\xff\u20ac', 7)
print('buffers', True)
print('text input', decode('\u00e9'), decode(r'\u0041'))
print('unused error name', encode(text, 'raw_missing'), decode(b'plain', 'raw_missing'))
for name in ('rawunicodeescape', 'raw_unicode.escape'):
    try:
        codecs.lookup(name)
    except LookupError:
        print('not an alias', name)
    else:
        raise AssertionError('unsupported alias accepted')
    try:
        text.encode(name)
    except LookupError:
        pass
    else:
        raise AssertionError('unsupported encode alias accepted')
