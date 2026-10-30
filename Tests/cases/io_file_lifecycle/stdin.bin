import _io as io
import posix
import _weakref
import gc

f = io.FileIO("lifetime.bin", "w+")
fd = f.fileno()
f.cycle = f
reference = _weakref.ref(f)
del f
gc.collect()
print(reference() is None)
try:
    posix.fstat(fd)
except OSError:
    print("descriptor closed")

class Derived(io.FileIO):
    def flush(self):
        gc.collect()
        raise RuntimeError("flush failed")

f = Derived("lifetime.bin")
fd = f.fileno()
try:
    f.close()
except RuntimeError as error:
    print(str(error), f.closed)
try:
    posix.fstat(fd)
except OSError:
    print("closed on flush error")
f.close()
posix.unlink("lifetime.bin")
