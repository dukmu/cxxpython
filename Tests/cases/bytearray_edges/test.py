class Source:
    def __bytes__(self):
        return b"wrong"

    def __iter__(self):
        return iter([65, 66])

print(bytearray(Source()))
b = bytearray(b"abcdef")
b[1:3] = b
print(b)
b[::2] = b""
print(b)
it = iter(b)
print(next(it))
b.append(33)
print(list(it))
try:
    b[::2] = b"x"
except ValueError:
    print("slice mismatch")
try:
    b.append(256)
except ValueError:
    print("invalid byte")
try:
    bytearray("text")
except TypeError:
    print("missing encoding")
try:
    hash(b)
except TypeError:
    print("unhashable")
b = bytearray(b"old")
try:
    b.__init__(-1)
except ValueError:
    print("failed init", b)
b = bytearray(b"old")
b.__init__(b)
print("self init", b)
