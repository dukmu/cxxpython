import _io as io
import gc

base = io._IOBase()
print(base.closed, base.readable(), base.writable(), base.seekable(), base.isatty())
for method, args in ((base.fileno, ()), (base.tell, ()), (base.seek, (0,)), (base.truncate, ()), (base._checkReadable, ()), (base._checkWritable, ())):
    try:
        method(*args)
    except io.UnsupportedOperation:
        print("unsupported")
base.close()
base.close()
print(base.closed)
try:
    base.readline(0)
except ValueError:
    print("closed readline")

class Raw(io._RawIOBase):
    def __init__(self):
        self.data = bytearray(b"a\nb\nc")
    def readinto(self, target):
        gc.collect()
        count = min(len(self.data), len(target))
        target[:count] = self.data[:count]
        del self.data[:count]
        return count
    def readable(self):
        return True

r = Raw()
print(r.read(2), r.readall(), r.read(0), r._checkReadable())
r = Raw()
print(r.readline(), r.readlines())

class Lines(io._IOBase):
    def __iter__(self):
        return iter([1, 2, 3])

print(Lines().readlines())
for method in (io._RawIOBase().readinto, io._RawIOBase().write):
    try:
        method(bytearray(1))
    except NotImplementedError:
        print("abstract raw method")

class Flush(io._IOBase):
    def flush(self):
        raise RuntimeError("flush")

f = Flush()
try:
    f.close()
except RuntimeError:
    print("failed flush", f.closed)
