import _io

raw = _io.BytesIO(b"one\ntwo\nthree")
reader = _io.BufferedReader(raw, 4)
print(reader.read(2), reader.tell(), reader.peek(1))
print(reader.readline(), reader.read(3), reader.tell())
print(reader.seek(0), reader.read(), reader.read())
reader.close()
print(reader.closed, raw.closed)
try:
    reader.read(1)
except Exception as error:
    print(type(error).__name__)

raw = _io.BytesIO()
writer = _io.BufferedWriter(raw, 4)
print(writer.write(b"abc"), raw.getvalue(), writer.tell())
print(writer.write(b"def"), raw.getvalue(), writer.tell())
writer.flush()
print(raw.getvalue(), writer.tell())
writer.close()
print(writer.closed, raw.closed)

raw = _io.BytesIO(b"abcdef")
stream = _io.BufferedRandom(raw, 4)
print(stream.read(2), stream.tell())
print(stream.write(b"XY"), stream.tell())
print(stream.seek(0), stream.read(), stream.tell())
stream.close()

source = _io.BytesIO(b"hello")
destination = _io.BytesIO()
pair = _io.BufferedRWPair(source, destination, 4)
print(pair.read(2), pair.write(b"abc"), destination.getvalue())
pair.flush()
print(pair.read(), destination.getvalue())
pair.close()
print(pair.closed, source.closed, destination.closed)
