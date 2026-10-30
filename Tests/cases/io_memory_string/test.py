import _io as io

for newline in (None, "", "\n", "\r", "\r\n"):
    s = io.StringIO("a\r\nb\rc\nd", newline=newline)
    print(repr(newline), repr(s.getvalue()), [repr(line) for line in s])
    print(repr(s.newlines))
    s.seek(0, 2)
    print(s.write("\nx"), repr(s.getvalue()))

s = io.StringIO()
s.writelines(["one\n", "two"])
print(s.tell(), s.seek(0), s.readline(), s.readlines())
print(s.seek(2), s.truncate(), repr(s.getvalue()))
with s as entered:
    print(entered is s, s.closed)
print(s.closed)
