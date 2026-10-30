import posix

names = [b'utf8-\xc3\xa9', b'bad-\xff', b'truncated-\xe2\x82',
         b'surrogate-\xed\xa0\x80', b'overlong-\xc0\xaf']
for name in names:
    fd = posix.open(name, posix.O_CREAT | posix.O_EXCL | posix.O_WRONLY, 0o600)
    posix.close(fd)

decoded = ['utf8-\u00e9', 'bad-\udcff', 'truncated-\udce2\udc82',
           'surrogate-\udced\udca0\udc80', 'overlong-\udcc0\udcaf']
for name in names:
    print(name in posix.listdir(b'.'))
for name in decoded:
    print(name in posix.listdir('.'), posix.stat(name).st_size)

posix.symlink(names[1], b'link')
print(posix.readlink(b'link') == names[1])
print(posix.readlink('link') == decoded[1])
posix.unlink('link')
for name in decoded:
    posix.unlink(name)
