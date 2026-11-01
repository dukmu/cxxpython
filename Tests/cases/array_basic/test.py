import array


print("typecodes", array.typecodes)
for code in "bBhHiIlLqQfd":
    a = array.array(code, [1, 2, 3])
    print(code, a.typecode, a.itemsize, len(a), list(a), list(a[::-1]))

a = array.array("i", [1, 2, 3, 4])
a[-1] = -5
a[1:3] = array.array("i", [8, 9, 10])
print("slice", list(a))
a[::2] = array.array("i", [7, 6, 5])
print("extended", list(a))
del a[1:3]
a.append(11)
a.extend([12, 13])
print("mutations", list(a))

b = array.array("i")
b.frombytes(a.tobytes())
print("bytes roundtrip", list(b) == list(a))
try:
    b.frombytes(b"x")
except ValueError:
    print("unaligned ValueError")

try:
    array.array("B", [256])
except OverflowError:
    print("overflow OverflowError")
try:
    a[0:1] = array.array("b", [1])
except TypeError:
    print("slice type TypeError")

u = array.array("u", "A\u03bb")
print("unicode", u.typecode, list(u), u.tounicode())

view = memoryview(a)
print("buffer", view.format, view.itemsize, view.readonly, view.tolist())
view[0] = 42
print("buffer write", a[0])
try:
    a.append(99)
except BufferError:
    print("export BufferError")
view.release()
a.append(99)
print("released", a[-1])
