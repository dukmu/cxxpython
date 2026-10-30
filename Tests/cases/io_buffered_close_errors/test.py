import _io

class Raw(_io._RawIOBase):
    def writable(self):
        return True
    def write(self, data):
        raise ValueError("write failed")
    def close(self):
        raise RuntimeError("close failed")

writer = _io.BufferedWriter(Raw(), 4)
writer.write(b"x")
try:
    writer.close()
except Exception as error:
    print(type(error).__name__, str(error))
    print(type(error.__context__).__name__, str(error.__context__))
