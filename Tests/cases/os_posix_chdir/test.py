import posix

original = posix.getcwdb()
fd = posix.open(b'.', posix.O_RDONLY)
posix.mkdir(b'dir-\xff')
posix.chdir(b'dir-\xff')
print(posix.getcwdb() == original + b'/dir-\xff')
print(posix.getcwd().endswith('dir-\udcff'))
posix.fchdir(fd)
print(posix.getcwdb() == original)
posix.chdir(fd)
print(posix.getcwdb() == original)
posix.close(fd)
posix.rmdir(b'dir-\xff')
try:
    posix.chdir(b'no-such-directory')
except OSError as error:
    print(error.filename == b'no-such-directory')
