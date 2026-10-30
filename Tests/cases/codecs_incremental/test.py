import codecs
for name in ('utf8', 'utf-8-sig', 'utf16', 'utf-16-le', 'utf-16-be', 'utf32', 'utf-32-le', 'utf-32-be'):
    source = 'A\u20ac\U0001f600z'
    raw = source.encode(name)
    decoder = codecs.getincrementaldecoder(name)()
    parts = [decoder.decode(raw[i:i+1]) for i in range(len(raw))]
    parts.append(decoder.decode(b'', True))
    encoder = codecs.getincrementalencoder(name)()
    encoded = b''.join(encoder.encode(part) for part in source) + encoder.encode('', True)
    print(name, ''.join(parts) == source, encoded == raw, decoder.getstate()[0])
    print(''.join(codecs.iterdecode([raw[:1], raw[1:]], name)) == source)
    print(b''.join(codecs.iterencode(source, name)) == raw)
