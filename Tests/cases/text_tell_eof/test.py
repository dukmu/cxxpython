import io


stream = io.TextIOWrapper(io.BytesIO(), encoding="utf-8")
stream.write("x" * 150)
stream.seek(0)
print("ascii", len(stream.read()), stream.tell())
stream.seek(stream.tell())
print("ascii eof", repr(stream.read()))


stream = io.TextIOWrapper(io.BytesIO("aé\r\nb".encode("utf-8")), encoding="utf-8")
print("unicode", repr(stream.read()), stream.tell())
stream.seek(0)
print("first", stream.read(2))
cookie = stream.tell()
tail = stream.read()
stream.seek(cookie)
print("roundtrip", repr(tail), stream.read() == tail, stream.tell())


def boundary(data, encoding, count):
    stream = io.TextIOWrapper(io.BytesIO(data), encoding=encoding, newline=None)
    prefix = stream.read(count)
    cookie = stream.tell()
    tail = stream.read()
    stream.seek(cookie)
    print(encoding, len(prefix), repr(tail), stream.read() == tail)


boundary(b"a" * 8191 + b"\xc3\xa9Z", "utf-8", 8191)
boundary(b"a" * 8191 + b"\r\nZ", "utf-8", 8191)
boundary(b"\xfe\xff" + b"\x00A" * 4095 + b"\x00B", "utf-16", 4095)
