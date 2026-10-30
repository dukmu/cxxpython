data = bytearray(b"\x00\x80\xff")
view = memoryview(data)
signed = view.cast("b")
chars = view.cast("c")
print(signed.tolist(), chars.tolist(), signed.format, chars.itemsize)
signed[0] = -1
chars[1] = b"A"
print(view.tolist(), data, list(view))
for value in (view, signed, chars):
    value.release()
data.append(1)
print(data)
