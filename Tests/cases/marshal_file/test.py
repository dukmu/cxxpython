import io
import marshal

stream = io.BytesIO()
marshal.dump([1, 'two', b'three'], stream)
marshal.dump(None, stream, 4)
stream.seek(0)
print(marshal.load(stream))
print(marshal.load(stream) is None)
try:
    marshal.load(stream)
except EOFError:
    print('EOFError')

class Reader:
    def __init__(self, data):
        self.data = data

    def read(self, count):
        result = self.data[:count]
        self.data = self.data[count:]
        return result

    def readinto(self, buffer):
        data = self.read(len(buffer))
        buffer[:len(data)] = data
        return len(data)

class Writer:
    def __init__(self):
        self.data = b''

    def write(self, data):
        self.data += data

writer = Writer()
print(marshal.dump((42, 'abc'), writer))
print(marshal.load(Reader(writer.data)))
