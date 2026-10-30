data = bytearray(b"abcdef")
view = memoryview(data)
part = view[::2]
print(view.obj is data, view.readonly, view.format, view.itemsize, view.ndim)
print(part.tobytes(), part.tolist(), part.hex(), part.shape, part.strides)
print(part.nbytes, part.c_contiguous, part.f_contiguous, part.contiguous)
part[1] = 88
print(data, view.tobytes())
part[:] = b"123"
print(data, view.tobytes())
readonly = part.toreadonly()
data[0] = 90
print(readonly.readonly, readonly.tobytes())
try:
    readonly[0] = 1
except TypeError:
    print("readonly")
try:
    part[:] = b"xx"
except ValueError:
    print("shape mismatch")
view.release()
print(part.tobytes())
part.release()
readonly.release()
data.append(33)
print(data)
