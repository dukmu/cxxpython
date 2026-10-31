import gc
import os

class Path:
    def __init__(self, path):
        self.path = path

    def __fspath__(self):
        gc.collect()
        return self.path

os.mkdir('entries')
fd = os.open(b'entries/raw-\xff', os.O_CREAT | os.O_WRONLY, 0o600)
os.close(fd)
try:
    for path in ('entries', b'entries', Path('entries'), Path(b'entries')):
        with os.scandir(path) as iterator:
            entry = next(iterator)
        expected = bytes if isinstance(os.fspath(path), bytes) else str
        print('path types', type(entry.name) is expected, type(entry.path) is expected,
              os.fsencode(entry.name) == b'raw-\xff', os.fsencode(entry.path) == b'entries/raw-\xff')
        print('path stat', entry.stat().st_size == 0)
    with os.scandir(path='entries/') as iterator:
        print('trailing slash', os.fsencode(next(iterator).path) == b'entries/raw-\xff')
    previous = os.getcwd()
    os.chdir('entries')
    try:
        for iterator in (os.scandir(), os.scandir(None)):
            with iterator:
                print('default path', os.fsencode(next(iterator).path) == b'./raw-\xff')
    finally:
        os.chdir(previous)
    for path, error in (('missing', FileNotFoundError), ('entries/raw-\udcff', NotADirectoryError)):
        try:
            os.scandir(path)
        except error as exc:
            print('path error', exc.filename == path)
    for path in (object(), 'bad\0path'):
        try:
            os.scandir(path)
        except (TypeError, ValueError) as exc:
            print(type(exc).__name__)
finally:
    os.unlink(b'entries/raw-\xff')
    os.rmdir('entries')
