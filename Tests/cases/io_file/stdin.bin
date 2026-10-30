import _io as io
import posix
import gc

with io.FileIO("data.bin", "w+") as f:
    print(isinstance(f, io._RawIOBase), f.mode, f.closed, f.closefd)
    print(f.readable(), f.writable(), f.seekable(), f.isatty())
    print(f.write(bytearray(b"abc\ndef\n")))
    print(f.seek(0), f.readline(), f.tell())
    print(f.readlines())
    print(f.seek(0), f.read(3), f.read())
    print(f.seek(1))
    data = bytearray(b"------")
    print(f.readinto(memoryview(data)[1:5]), data)
    print(f.truncate(3), f.seek(0), f.readall())
    f.seek(0, 2)
    f.writelines([b"!", memoryview(b"?")])
    f.seek(0)
    print(f.read())
print(f.closed)
print(repr(f))
f.close()
try:
    f.read(0)
except ValueError:
    print("closed read")

fd = posix.open("data.bin", posix.O_RDONLY)
f = io.FileIO(fd, closefd=False)
print(f.name == fd, f.closefd, f.read(2))
f.close()
print(posix.lseek(fd, 0, 0), posix.read(fd, 3))
print(f.closefd, f.mode)
posix.close(fd)

class Path:
    def __fspath__(self):
        return "data.bin"

p = Path()
with io.FileIO(p) as f:
    print(f.name is p, f.read())

def opener(path, flags):
    print(path, bool(flags & posix.O_CLOEXEC))
    gc.collect()
    return posix.open(path, flags)

with io.FileIO("data.bin", opener=opener) as f:
    print(f.read())
posix.unlink("data.bin")
