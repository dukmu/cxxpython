import os
import posix

os.mkdir('entries')
os.mkdir('entries/sub')
with open('entries/file', 'wb') as stream:
    stream.write(b'abc')
os.symlink('file', 'entries/link')
os.symlink('missing', 'entries/broken')
try:
    with os.scandir('entries') as iterator:
        print('iterator', iter(iterator) is iterator, iterator.__enter__() is iterator)
        entries = {entry.name: entry for entry in iterator}
    print('names', sorted(entries))
    print('exports', os.DirEntry is posix.DirEntry, 'DirEntry' in os.__all__, 'scandir' in os.__all__)
    for name in sorted(entries):
        entry = entries[name]
        print(name, isinstance(entry, os.DirEntry), entry.path == 'entries/' + name,
              os.fspath(entry) == entry.path, repr(entry) == '<DirEntry ' + repr(name) + '>')
        print('kind', entry.is_dir(), entry.is_file(), entry.is_symlink(),
              entry.is_dir(follow_symlinks=False), entry.is_file(follow_symlinks=False))
        print('inode', entry.inode() == os.lstat(entry.path).st_ino)
    print('closed', list(iterator))
    iterator.close()
    print('reclosed', list(iterator))
    try:
        with os.scandir('entries') as early:
            next(early)
            raise ValueError('leave')
    except ValueError:
        print('context exception', list(early))
finally:
    for name in ('file', 'link', 'broken'):
        os.unlink('entries/' + name)
    os.rmdir('entries/sub')
    os.rmdir('entries')
