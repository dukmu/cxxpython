import os

p = os.path
print(p.join('/a', 'b', '..', 'c'))
print(p.normpath('//a///b/../c/'))
print(p.split('/a/b/'), p.splitext('/a/.profile'))
print(p.basename(b'/a/\xff'), p.dirname(b'/a/\xff'))
print(p.commonpath(['/a/b', '/a/c']))
print(p.relpath('/a/b/c', '/a/d'))
fd = os.open(b'file-\xff', os.O_CREAT | os.O_WRONLY | os.O_EXCL, 0o600)
os.write(fd, b'abc')
os.close(fd)
os.symlink(b'file-\xff', b'link')
print(p.exists(b'link'), p.lexists(b'link'), p.islink(b'link'))
print(p.isfile(b'link'), p.getsize(b'link'))
print(p.realpath(b'link') == p.join(os.getcwdb(), b'file-\xff'))
os.unlink(b'file-\xff')
print(p.exists(b'link'), p.lexists(b'link'))
os.unlink(b'link')
