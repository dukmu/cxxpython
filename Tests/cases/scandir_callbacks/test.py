import gc
import os

os.mkdir('entries')
with open('entries/file', 'w') as stream:
    stream.write('content')

class Path:
    def __fspath__(self):
        gc.collect()
        return ''.join(['entr', 'ies'])

class Follow:
    def __init__(self, entry):
        self.entry = entry

    def __bool__(self):
        gc.collect()
        assert self.entry.stat(follow_symlinks=False).st_size == 7
        return True

class BadFollow:
    def __bool__(self):
        gc.collect()
        raise ValueError('callback')

try:
    with os.scandir(Path()) as iterator:
        entry = next(iterator)
    del iterator
    gc.collect()
    print('callback', entry.stat(follow_symlinks=Follow(entry)).st_size,
          entry.is_file(follow_symlinks=Follow(entry)), entry.is_dir(follow_symlinks=Follow(entry)))
    for method in (entry.stat, entry.is_file, entry.is_dir):
        try:
            method(follow_symlinks=BadFollow())
        except ValueError as exc:
            print('callback error', str(exc))
    cached = entry.stat()
    for i in range(20):
        gc.collect()
        assert entry.stat() is cached
    print('cache rooted', entry.name, entry.path, cached.st_size)
finally:
    os.unlink('entries/file')
    os.rmdir('entries')
