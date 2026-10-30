import _io

raw = _io.BytesIO('abc\r\ndef'.encode('utf-8'))
f = _io.TextIOWrapper(_io.BufferedReader(raw), encoding='utf-8', newline=None)
print(f.readable(), f.writable(), f.seekable())
print(repr(f.read(2)))
p = f.tell()
print(repr(f.readline()))
f.seek(p)
print(repr(f.read()))
try:
    f.write('x')
except Exception as e:
    print(type(e).__name__)
f.close()
f.close()
print(f.closed)

raw = _io.BytesIO()
f = _io.TextIOWrapper(_io.BufferedWriter(raw), encoding='utf-16')
print(f.readable(), f.writable())
print(f.write('ab'))
f.flush()
print(repr(raw.getvalue()))
print(f.tell())
try:
    f.read()
except Exception as e:
    print(type(e).__name__)
f.seek(0)
f.write('c')
f.flush()
print(repr(raw.getvalue()))
f.detach()
print(f.encoding, f.errors)
