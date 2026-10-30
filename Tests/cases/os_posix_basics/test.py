import posix

print(isinstance(posix._have_functions, list))
print(all(isinstance(key, bytes) and isinstance(value, bytes)
          for key, value in posix.environ.items()))

class Path:
    def __fspath__(self):
        return b"sample"

print(posix.fspath(Path()))
fd = posix.open("sample", posix.O_CREAT | posix.O_RDWR | posix.O_EXCL, 0o600)
print(posix.write(fd, b"abc"))
print(posix.stat(fd).st_size, posix.fstat(fd).st_size, posix.stat("sample").st_size)
print(posix.lseek(fd, 0, 0), posix.read(fd, 3))
posix.close(fd)
posix.symlink("sample", "link")
print(posix.readlink("link"), posix.readlink(b"link"))
print(posix.stat("link").st_size == posix.stat("sample").st_size)
print(posix.lstat("link").st_ino != posix.stat("link").st_ino)
posix.unlink("link")
posix.unlink("sample")
