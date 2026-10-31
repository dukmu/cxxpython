import gc
import os
import stat

os.mkdir('entries')
with open('entries/file', 'wb') as stream:
    stream.write(b'abc')
os.symlink('file', 'entries/link')
os.symlink('missing', 'entries/broken')
try:
    with os.scandir('entries') as iterator:
        entries = {entry.name: entry for entry in iterator}
    file = entries['file']
    cached = file.stat()
    gc.collect()
    print('record', type(cached) is os.stat_result, cached.st_size == 3,
          cached.st_mtime_ns == os.stat(file.path).st_mtime_ns, len(cached) == 10)
    print('shared cache', cached is file.stat(), cached is file.stat(follow_symlinks=False))
    with open(file.path, 'ab') as stream:
        stream.write(b'def')
    print('cached metadata', file.stat().st_size == 3, os.stat(file.path).st_size == 6)
    link = entries['link']
    nofollow = link.stat(follow_symlinks=False)
    follow = link.stat()
    gc.collect()
    print('link caches', nofollow is link.stat(follow_symlinks=False), follow is link.stat(),
          nofollow is not follow, stat.S_ISLNK(nofollow.st_mode), stat.S_ISREG(follow.st_mode))
    broken = entries['broken']
    try:
        broken.stat()
    except FileNotFoundError as exc:
        print('broken stat', exc.filename == broken.path)
    with open('entries/missing', 'wb') as stream:
        stream.write(b'x')
    print('failed stat not cached', broken.stat().st_size == 1)
    os.unlink('entries/missing')
    for name in ('name', 'path'):
        try:
            setattr(file, name, 'changed')
        except AttributeError:
            print('readonly', name)
    try:
        os.DirEntry()
    except TypeError:
        print('no public constructor')
    try:
        class Child(os.DirEntry):
            pass
    except TypeError:
        print('no subclass')
    for method in (file.stat, file.is_dir, file.is_file):
        try:
            method(False)
        except TypeError:
            print('keyword only')
    os.unlink(file.path)
    print('retained cache', file.stat() is cached)
finally:
    for name in ('file', 'link', 'broken', 'missing'):
        if os.path.lexists('entries/' + name):
            os.unlink('entries/' + name)
    os.rmdir('entries')
