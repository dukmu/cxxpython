from array import array


def failing():
    yield 4
    yield 5
    raise RuntimeError("stopped")


a = array("i", [1, 2, 3])
try:
    a.extend(failing())
except RuntimeError as exc:
    print("iterator error", str(exc), list(a))

try:
    a.extend([6, "bad", 7])
except TypeError:
    print("conversion error", list(a))

a.extend(a)
print("self extend", list(a))
try:
    a.extend(array("b", [1]))
except TypeError:
    print("different type", list(a))

v = memoryview(a)
print("view", v.format, v.itemsize, v.readonly, v[0], v[-1])
v[0] = 42
a[1] = 43
print("shared", a[0], v[1])
try:
    a.extend([99])
except BufferError:
    print("resize blocked", len(a))
child = v[1:3]
v.release()
try:
    a.append(99)
except BufferError:
    print("child holds export", child[0])
child.release()
a.append(99)
print("released", a[-1])

u = memoryview(array("u", "ab"))
print("unicode buffer", u.format, u.itemsize, u.nbytes)
try:
    u[0]
except NotImplementedError:
    print("unicode scalar unsupported")
u.release()
