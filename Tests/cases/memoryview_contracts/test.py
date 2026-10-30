import _codecs

raw = memoryview(b"\x01\xff")
signed = raw.cast("b")
chars = raw.cast("c")
print(raw == signed, raw != signed, raw == b"\x01\xff", chars == raw)
print(raw[:1] == signed[:1], memoryview(b"") == memoryview(b"").cast("c"))
print(hash(raw) == hash(b"\x01\xff"), hash(raw) == hash(signed), hash(raw) == hash(chars))
print(repr(raw).startswith("<memory at 0x"), repr(raw).endswith(">"))
mutable = bytearray(b"abcde")
stride = memoryview(mutable)[::2]
print(bytearray(stride), bytearray(stride.cast("b")) if stride.c_contiguous else bytearray(memoryview(mutable).cast("b")[::2]))
try:
    _codecs.utf_8_decode(stride)
except BufferError:
    print("noncontiguous")
try:
    hash(stride)
except ValueError:
    print("writable")
try:
    hash(stride.toreadonly())
except TypeError:
    print("unhashable exporter")
raw.release()
print(raw == raw, raw == signed, raw != signed, repr(raw).startswith("<released memory at 0x"))
