data = bytearray(b"abc")
view = memoryview(data)
child = view[1:]
for action in (lambda: data.append(100), lambda: data.pop(), lambda: data.clear(),
               lambda: data.__delitem__(0), lambda: data.__setitem__(slice(0, 1), b"xy")):
    try:
        action()
    except BufferError:
        print("buffer error")
data[0] = 90
data.reverse()
print(view.tobytes(), child.tobytes())
view.release()
try:
    data.append(100)
except BufferError:
    print("child holds export")
child.release()
data.append(100)
print(data)
try:
    view.tobytes()
except ValueError:
    print("released")
with memoryview(b"xyz") as immutable:
    print(immutable.readonly, immutable.obj, immutable.suboffsets)
try:
    immutable.tolist()
except ValueError:
    print("context released")
