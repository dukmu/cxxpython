import _io

class ShortWriter(_io._RawIOBase):
    def __init__(self):
        self.data = bytearray()
    def writable(self):
        return True
    def write(self, data):
        count = min(2, len(data))
        self.data.extend(data[:count])
        return count

raw = ShortWriter()
stream = _io.BufferedWriter(raw, 4)
print(stream.write(b"abcdef"))
stream.flush()
print(bytes(raw.data))
print(stream.detach() is raw)
try:
    stream.write(b"x")
except Exception as error:
    print(type(error).__name__)

class WouldBlock(_io._RawIOBase):
    def readable(self):
        return True
    def readinto(self, data):
        return None

reader = _io.BufferedReader(WouldBlock(), 4)
print(reader.read(1), reader.read1(1), reader.peek(), reader.readline())

raw = _io.BytesIO(b"x")
reader = _io.BufferedReader(raw)
reader.close()
print(reader.raw is raw)

raw = _io.BytesIO(b"abcdef")
reader = _io.BufferedReader(raw, 3)
target = bytearray(2)
print(reader.readinto(target), bytes(target))
print(reader.readinto1(target), bytes(target))
print(reader.read())

class Reentrant(_io._RawIOBase):
    def readable(self):
        return True
    def readinto(self, data):
        self.stream.read(1)
        return 0

raw = Reentrant()
reader = _io.BufferedReader(raw, 4)
raw.stream = reader
try:
    reader.read(1)
except Exception as error:
    print(type(error).__name__)
