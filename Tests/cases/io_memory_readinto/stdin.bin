import _io as io

stream = io.BytesIO(b"abcdef")
target = bytearray(3)
print(stream.readinto(target), bytes(target), stream.tell())
target.append(88)
print(bytes(target))
print(stream.readinto1(target), bytes(target), stream.tell())
target.append(89)
print(bytes(target), stream.readinto(target))

stream = io.BytesIO(b"abcdef")
view = stream.getbuffer()
part = view[2:5]
print(stream.readinto(part), stream.tell(), stream.getvalue())
part.release()
view.release()
print(stream.write(b"Z"), stream.getvalue())
