import io
import os
import builtins

print(io.open is builtins.open)
with open('data.bin', 'wb') as stream:
    print(isinstance(stream, io.BufferedIOBase), stream.write(b'abcdef'), stream.name)
with io.open('data.bin', 'rb', buffering=3) as stream:
    print(stream.read(2), stream.tell(), stream.read())
with open('data.bin', 'r+b', buffering=4) as stream:
    print(type(stream) is io.BufferedRandom)
    stream.seek(2)
    stream.write(b'XY')
with io.open_code('data.bin') as stream:
    print(stream.read())
with open('data.txt', 'w', encoding='utf-16', newline='\r\n') as stream:
    print(isinstance(stream, io.TextIOBase), stream.write('A\n\u03b2'), stream.mode)
with open('data.txt', encoding='utf-16') as stream:
    print(ascii(stream.read()), repr(stream.newlines))
with open('data.bin', 'rb', buffering=0) as stream:
    print(type(stream) is io.FileIO, stream.read(1))
fd = os.open('data.bin', os.O_RDONLY)
with open(fd, 'rb', closefd=False) as stream:
    print(stream.read(1), stream.name == fd)
print(os.read(fd, 1))
os.close(fd)
