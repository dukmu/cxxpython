import _io

for encoding in ('utf-8', 'utf-16'):
    raw = _io.BytesIO('a中\r\nb\rc\n'.encode(encoding))
    f = _io.TextIOWrapper(raw, encoding=encoding, newline='')
    print(encoding)
    print(repr(f.read(1)))
    p = f.tell()
    print(repr(f.read(3)))
    f.seek(p)
    print(repr(f.read(3)))
    print(repr(f.readline()))
    p = f.tell()
    print(repr(f.read()))
    f.seek(p)
    print(repr(f.read()))
    f.seek(0, 2)
    print(repr(f.read()))
