import _io

for encoding in ('utf-16', 'utf-8-sig'):
    initial = 'A'.encode(encoding)
    raw = _io.BytesIO(initial)
    raw.seek(0, 2)
    text = _io.TextIOWrapper(raw, encoding=encoding)
    text.write('B')
    text.flush()
    print(encoding, raw.getvalue() == 'AB'.encode(encoding))
    try:
        print(text.name)
    except AttributeError:
        print('no name')
    text.detach()

raw = _io.FileIO('/dev/null', 'rb')
text = _io.TextIOWrapper(raw, encoding='utf-8')
print(text.name)
text.close()
