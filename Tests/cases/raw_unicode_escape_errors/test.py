import codecs

decode = codecs.raw_unicode_escape_decode
bad = (b'pre\\u12', br'[\u12Z4]', br'\U00110000', br'\Uffffffff')
for raw in bad:
    try:
        decode(raw)
    except UnicodeDecodeError as exc:
        assert exc.object == raw
        print('strict', raw, exc.encoding, exc.start, exc.end, exc.reason, str(exc))
    else:
        raise AssertionError(raw)
    for errors in ('ignore', 'replace', 'backslashreplace'):
        result, consumed = decode(raw, errors)
        assert consumed == len(raw)
        print(errors, repr(result), consumed)
for errors in ('surrogatepass', 'surrogateescape', 'xmlcharrefreplace', 'namereplace'):
    try:
        decode(br'\uZ', errors)
    except (UnicodeDecodeError, TypeError) as exc:
        print('inapplicable', errors, type(exc).__name__)
    else:
        raise AssertionError(errors)
try:
    decode(br'\uZ', 'raw_missing')
except LookupError:
    print('missing handler', True)
else:
    raise AssertionError('unknown error handler accepted')
