from pathlib import PurePosixPath, PureWindowsPath


class PathLike:
    def __fspath__(self):
        print('fspath')
        return 'folder/file.tar.gz'


for cls, value in ((PurePosixPath, '/a//b/../file.tar.gz'),
                   (PureWindowsPath, r'C:\a\b\file.tar.gz'),
                   (PureWindowsPath, r'\\server\share\dir')):
    path = cls(value)
    print(str(path), path.parts)
    print(path.drive, path.root, path.anchor, path.name)
    print(path.suffix, path.suffixes, path.stem)
    print(str(path.parent), path.is_absolute())
    print(str(path / 'child'), path.match('*.gz'))

path = PurePosixPath(PathLike())
print(path, path.with_name('other.txt'), path.with_suffix('.zip'))
print(PurePosixPath('/a/b/c').relative_to('/a'))
print(PurePosixPath('/a/b').as_uri())
try:
    PurePosixPath('/a').relative_to('/b')
except ValueError as error:
    print(type(error).__name__)
