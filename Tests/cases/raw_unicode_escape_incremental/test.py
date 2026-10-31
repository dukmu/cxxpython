import codecs
import io

decode = codecs.raw_unicode_escape_decode
for raw in (b'prefix\\', br'A\u12', br'A\U0001f', br'A\uZ', br'\\', br'\u0041'):
    print('partial', raw, decode(raw, 'replace', False))
    print('final', raw, decode(raw, 'replace', True))
raw = b'A\\u20ac\\U0001f600\\\\u0041\\'
expected = raw.decode('raw-unicode-escape')
for split in range(len(raw) + 1):
    decoder = codecs.getincrementaldecoder('raw-unicode-escape')()
    first = decoder.decode(raw[:split])
    state = decoder.getstate()
    second = decoder.decode(raw[split:], True)
    assert first + second == expected
    restored = codecs.getincrementaldecoder('raw-unicode-escape')()
    restored.setstate(state)
    assert first + restored.decode(raw[split:], True) == expected
    assert restored.getstate() == (b'', 0)
print('all split points', len(raw) + 1)
decoder = codecs.getincrementaldecoder('raw-unicode-escape')()
assert decoder.decode(br'\u12') == ''
print('buffered state', decoder.getstate())
decoder.reset()
assert decoder.decode(b'ok', True) == 'ok'
decoder = codecs.getincrementaldecoder('raw-unicode-escape')('replace')
assert decoder.decode(br'\u12') == ''
print('final incomplete', repr(decoder.decode(b'', True)))
text = 'A\u20ac\U0001f600\ud800'
encoder = codecs.getincrementalencoder('raw-unicode-escape')()
assert b''.join(encoder.encode(ch) for ch in text) + encoder.encode('', True) == text.encode('raw-unicode-escape')
assert ''.join(codecs.iterdecode([raw[i:i+1] for i in range(len(raw))], 'raw-unicode-escape')) == expected
print('incremental encoder and iterator', True)
stream = io.BytesIO()
writer = codecs.getwriter('raw-unicode-escape')(stream)
writer.write(text)
reader = codecs.getreader('raw-unicode-escape')(io.BytesIO(stream.getvalue()))
assert reader.read() == text
print('stream roundtrip', True)
