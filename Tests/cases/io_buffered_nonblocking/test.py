import _io

class Raw(_io._RawIOBase):
    def __init__(self):
        self.blocked = True
        self.data = bytearray()
    def writable(self):
        return True
    def write(self, data):
        if self.blocked:
            return None
        count = min(len(data), 2)
        self.data.extend(data[:count])
        return count

raw = Raw()
writer = _io.BufferedWriter(raw, 4)
try:
    writer.write(b"abcdef")
except BlockingIOError as error:
    print(type(error).__name__, error.characters_written, error.args)
try:
    writer.flush()
except BlockingIOError as error:
    print(type(error).__name__, error.characters_written)
raw.blocked = False
writer.flush()
print(bytes(raw.data))
print(writer.write(b"ef"))
writer.flush()
print(bytes(raw.data))

raw = Raw()
writer = _io.BufferedWriter(raw, 4)
print(writer.write(b"ab"))
try:
    writer.write(b"cde")
except BlockingIOError as error:
    print(error.characters_written)
raw.blocked = False
writer.flush()
print(bytes(raw.data))
