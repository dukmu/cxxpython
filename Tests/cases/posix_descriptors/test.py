import posix

name = b"posix_fixture_file"
fd = posix.open(name, posix.O_CREAT | posix.O_EXCL | posix.O_RDWR, 0o600)
print(posix.write(fd, b"hello"))
print(posix.write(fd, bytearray(b"!")))
print(posix.lseek(fd, 0, 0))
print(posix.read(fd, 8))
posix.close(fd)
posix.rename(name, b"posix_fixture_renamed")
posix.unlink(b"posix_fixture_renamed")
print(posix.fspath(b"path"))
print(posix.getpid() > 0, posix.getppid() > 0)
try:
    posix.open(b"bad\x00path", posix.O_RDONLY)
except ValueError:
    print("nul rejected")
try:
    posix.close(-1)
except OSError as error:
    print(error.args[0] > 0)
