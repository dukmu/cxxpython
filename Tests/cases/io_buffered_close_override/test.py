import _io

class Writer(_io.BufferedWriter):
    def flush(self):
        print("custom flush")
        return super().flush()

raw = _io.BytesIO()
writer = Writer(raw, 4)
writer.write(b"abc")
writer.close()
print(writer.closed, raw.closed)
