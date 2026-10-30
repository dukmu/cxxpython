import gc
import io
import os

calls = []
class Path:
    def __fspath__(self):
        gc.collect()
        calls.append('path')
        return 'callback.bin'
class Size:
    def __index__(self):
        gc.collect()
        calls.append('size')
        return 4
def opener(path, flags):
    gc.collect()
    calls.append(path)
    return os.open(path, flags, 0o600)
with open(Path(), 'wb', buffering=Size(), opener=opener) as stream:
    print(stream.write(b'data'))
print(calls)

original = io.FileIO
io.FileIO = None
try:
    with open('callback.bin', 'rb') as stream:
        print(stream.read())
finally:
    io.FileIO = original

descriptors = []
def remember(path, flags):
    fd = os.open(path, flags)
    descriptors.append(fd)
    return fd
try:
    open('callback.bin', encoding='unknown-file-codec', opener=remember)
except LookupError:
    print('codec failed')
try:
    os.fstat(descriptors[0])
except OSError:
    print('closed after failure')
