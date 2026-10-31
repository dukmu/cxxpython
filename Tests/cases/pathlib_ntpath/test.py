import ntpath


class Path:
    def __fspath__(self):
        print('fspath called')
        return 'C:\\folder\\file.txt'


for path in ('C:\\folder\\file.txt', r'\\server\share\dir\file',
             'relative/../item', r'\rooted', ''):
    print('path', repr(path))
    print('drive', ntpath.splitdrive(path))
    print('split', ntpath.split(path))
    print('norm', ntpath.normpath(path))
    print('absolute', ntpath.isabs(path))

print('join', ntpath.join('C:\\one', 'two', '..', 'three'))
print('join drive', ntpath.join('C:\\one', 'D:two'))
print('extension', ntpath.splitext(Path()))
print('relative', ntpath.relpath('C:\\one\\two', 'C:\\one'))
try:
    ntpath.relpath('D:\\one', 'C:\\one')
except ValueError as error:
    print(type(error).__name__, str(error))
