b = bytearray(b"abcd")
print(b, len(b), bool(b), b[0], b[-1], b[1:3])
b[1] = 120
b[2:3] = [49, 50]
print(b)
b[::2] = b"XYZ"
print(b)
del b[::-2]
print(b)
b.append(33)
b.extend([63, 46])
b.insert(-1, 35)
print(b, b.pop(), b.pop(0))
b.remove(63)
b.reverse()
print(b, b.copy(), b + b"!", b * 2)
c = b
c += b"z"
c *= 2
print(c is b, b)
print(35 in b, b"z#" in b, b == bytes(b), b < b"z")
print(bytearray(3), bytearray("h\u00e9", "utf-8").decode())
it = iter(bytearray(b"ab"))
print(next(it), next(it))
try:
    next(it)
except StopIteration:
    print("exhausted")
b.clear()
print(b, bool(b))
