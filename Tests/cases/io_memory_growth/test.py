import _io as io

for constructor, unit in ((io.BytesIO, b"abc\n"), (io.StringIO, "abc\n")):
    stream = constructor()
    for index in range(1000):
        stream.write(unit)
    print(stream.tell(), len(stream.getvalue()))
    stream.seek(3996)
    print(stream.readline() == unit, stream.read() == unit[:0])
    stream.seek(5000)
    stream.write(unit)
    data = stream.getvalue()
    zero = b"\0" if isinstance(unit, bytes) else "\0"
    print(len(data), data[3999:4001] == unit[-1:] + zero)
