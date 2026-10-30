import posix

class Path:
    def __fspath__(self):
        return b"posix_tree"

root = Path()
posix.mkdir(root)
fd = posix.open(b"posix_tree/file", posix.O_CREAT | posix.O_WRONLY, 0o600)
posix.write(fd, b"abc")
s = posix.fstat(fd)
posix.close(fd)
posix.symlink(b"file", b"posix_tree/link")
print(posix.fspath(root))
print(sorted(posix.listdir(root)))
print(posix.readlink(b"posix_tree/link"))
print(posix.stat(b"posix_tree/link").st_size, posix.lstat(b"posix_tree/link").st_size)
print(isinstance(s, tuple), type(s) is posix.stat_result, len(s), s[6], s.st_size)
print(type(s.st_mtime_ns) is int, type(s.st_mtime) is float)
constructed = posix.stat_result((0, 1, 2, 3, 4, 5, 6, 7, 8, 9))
print(len(constructed), constructed.st_size, constructed.st_atime, constructed.st_atime_ns)
try:
    posix.stat_result((1, 2))
except TypeError:
    print("invalid stat sequence")
print(type(posix.getcwd()) is str, type(posix.getcwdb()) is bytes)
try:
    posix.stat(b"posix_tree/missing")
except OSError as error:
    print(error.errno > 0, error.filename == b"posix_tree/missing")
try:
    posix.rename(b"posix_tree/missing", b"posix_tree/destination")
except FileNotFoundError as error:
    print(error.filename == b"posix_tree/missing", error.filename2 == b"posix_tree/destination")
posix.unlink(b"posix_tree/link")
posix.unlink(b"posix_tree/file")
posix.rmdir(root)
print(type(posix.environ) is dict)
posix.putenv(b"CXXPYTHON_POSIX_TEST", b"value")
posix.unsetenv(b"CXXPYTHON_POSIX_TEST")
try:
    posix.stat(root)
except FileNotFoundError as error:
    print(error.filename == b"posix_tree")
