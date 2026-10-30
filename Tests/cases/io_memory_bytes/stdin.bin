import _io as io

s = io.BytesIO(b"one\ntwo")
print(s.readline(), s.tell(), s.readlines())
print(s.seek(1), s.write(b"XY"), s.getvalue())
v = s.getbuffer()
v[0] = 65
print(s.getvalue(), bytes(v))
for operation in (lambda: s.write(b"z"), s.truncate, s.close):
    try:
        operation()
    except Exception as error:
        print(type(error).__name__)
v.release()
print(s.seek(0), list(s))
with s as entered:
    print(entered is s, s.closed)
print(s.closed)
try:
    s.getvalue()
except Exception as error:
    print(type(error).__name__)
