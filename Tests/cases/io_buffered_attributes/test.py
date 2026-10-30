import _io
import gc

class Raw(_io.BytesIO):
    @property
    def name(self):
        gc.collect()
        return self.stream.read(1)

    @property
    def mode(self):
        gc.collect()
        return "custom"

raw = Raw(b"abc")
stream = _io.BufferedReader(raw, 2)
raw.stream = stream
print(stream.name, stream.mode, stream.read())
stream.detach()
try:
    print(stream.name)
except Exception as error:
    print(type(error).__name__)

class Named(_io.BytesIO):
    name = "example"
    mode = "rb+"

for kind in (_io.BufferedWriter, _io.BufferedRandom):
    raw = Named()
    stream = kind(raw)
    print(stream.name, stream.mode)
    stream.close()
    print(stream.name, stream.mode)

pair = _io.BufferedRWPair(_io.BytesIO(), _io.BytesIO())
print(hasattr(pair, "name"), hasattr(pair, "mode"))
