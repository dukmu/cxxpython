import gc
import threading
from pathlib import Path, PurePosixPath, PureWindowsPath
from urllib.parse import quote_from_bytes


class UriPath(PurePosixPath):
    def __str__(self):
        gc.collect()
        return super().__str__()


class Safe:
    def __iter__(self):
        gc.collect()
        yield ord('/')


class CallbackError(Exception):
    pass


class FailingUriPath(PurePosixPath):
    def __str__(self):
        gc.collect()
        raise callback_error


callback_error = CallbackError('uri callback')
# Leave these shared caches cold until the workers start together.
shared = (PurePosixPath('/shared/a b/file.txt'),
          PureWindowsPath('C:/Shared/a b/File.txt'))
expected = (('/shared/a b/file.txt', ('/', 'shared', 'a b', 'file.txt'),
             '/shared/a b/other.txt', '/shared', 'a b/file.txt'),
            ('C:\\Shared\\a b\\File.txt', ('C:\\', 'Shared', 'a b', 'File.txt'),
             'C:\\Shared\\a b\\other.txt', 'c:/shared', 'a b\\File.txt'))
root = Path('pathlib-parallel')
root.mkdir()
for index in range(4):
    directory = root / str(index)
    directory.mkdir()
    (directory / 'nested').mkdir()
    (directory / 'alpha.txt').write_text('alpha ' + str(index), encoding='utf-8')
    (directory / 'nested' / 'beta.txt').write_text('beta ' + str(index), encoding='utf-8')

start = threading.Barrier(4)
results = [False] * 4
errors = [None] * 4


def worker(index):
    try:
        start.wait(5)
        directory = root / str(index)
        for turn in range(2):
            for path, values in zip(shared, expected):
                text, parts, renamed, base, relative = values
                assert str(path) == text
                assert path.parts == parts
                assert hash(path) == hash(type(path)(text))
                assert str(path.with_name('other.txt')) == renamed
                assert str(path.relative_to(base)) == relative
            uri = UriPath('/a b/caf\u00e9?#.txt')
            assert uri.as_uri() == 'file:///a%20b/caf%C3%A9%3F%23.txt'
            assert quote_from_bytes(b'/a b/\xff?#', Safe()) == '/a%20b/%FF%3F%23'
            try:
                FailingUriPath('/failure').as_uri()
            except CallbackError as error:
                assert error is callback_error
            else:
                raise AssertionError('URI callback error was lost')
            assert (directory / 'alpha.txt').read_text(encoding='utf-8') == 'alpha ' + str(index)
            assert (directory / 'nested' / 'beta.txt').read_text(encoding='utf-8') == 'beta ' + str(index)
            assert sorted(path.relative_to(directory).as_posix()
                          for path in directory.glob('**/*.txt')) == ['alpha.txt', 'nested/beta.txt']
        results[index] = True
    except BaseException as error:
        errors[index] = (type(error).__name__, str(error))


threads = [threading.Thread(target=worker, args=(index,), daemon=True)
           for index in range(4)]
for thread in threads:
    thread.start()
for thread in threads:
    thread.join(5)
joined = not any(thread.is_alive() for thread in threads)
print('bounded joins', joined)
print('shared paths, callbacks, files and glob', results)
print('errors', errors)
assert joined and all(results) and errors == [None] * 4

for index in range(4):
    directory = root / str(index)
    (directory / 'alpha.txt').unlink()
    (directory / 'nested' / 'beta.txt').unlink()
    (directory / 'nested').rmdir()
    directory.rmdir()
root.rmdir()
