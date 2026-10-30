import _io

f = _io.TextIOWrapper(_io.BytesIO(b'abc'), encoding='utf-8')
for offset, whence in ((-1, 0), (1, 1), (1, 2), (0, 3)):
    try:
        f.seek(offset, whence)
    except Exception as e:
        print(type(e).__name__)
print(f.seek(0, 1))
print(repr(f.read(1)))
print(f.seek(0, 1) == f.tell())
f.close()
for method in (f.read, f.flush, f.tell):
    try:
        method()
    except Exception as e:
        print(type(e).__name__)
try:
    f.write('x')
except Exception as e:
    print(type(e).__name__)
