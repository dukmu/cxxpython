import _io as io
import posix

for mode in ("", "rr", "r++", "rt", "rw", "r\x00"):
    try:
        io.FileIO("unused.bin", mode)
    except ValueError:
        print("invalid mode")
for function, error in (
    (lambda: io.FileIO("missing.bin"), FileNotFoundError),
    (lambda: io.FileIO("."), IsADirectoryError),
    (lambda: io.FileIO(-1), ValueError),
    (lambda: io.FileIO("unused.bin", closefd=False), ValueError),
    (lambda: io.FileIO("unused.bin", opener=lambda *args: -1), ValueError),
    (lambda: io.FileIO("unused.bin", opener=lambda *args: "bad"), TypeError),
):
    try:
        function()
    except error:
        print(error.__name__)
with io.FileIO("value.bin", "w") as f:
    print(f.readable(), f.writable())
    try:
        f.read()
    except io.UnsupportedOperation as e:
        print(isinstance(e, OSError), isinstance(e, ValueError))
    try:
        f.truncate(-1)
    except OSError:
        print("negative truncate")
with io.FileIO("value.bin") as f:
    for buffer in (b"readonly", memoryview(bytearray(8))[::2]):
        try:
            f.readinto(buffer)
        except TypeError:
            print("invalid readinto")
    target = bytearray(4)
    print(f.readinto(target))
    target.append(1)
    print(len(target))
posix.unlink("value.bin")
