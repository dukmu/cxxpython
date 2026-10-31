import os
import shutil

directory = '/tmp/cxxpython-shutil-case-' + str(os.getpid())
os.mkdir(directory)
try:
    source = os.path.join(directory, 'source')
    target = os.path.join(directory, 'target')
    with open(source, 'w') as stream:
        stream.write('hello\n')
    shutil.copyfile(source, target)
    with open(target) as stream:
        print(stream.read(), end='')
    with os.scandir(directory) as entries:
        source_entry = next(entry for entry in entries if entry.name == 'source')
    shutil.copyfile(source_entry, target)
    with open(target) as stream:
        print('DirEntry copy', stream.read() == 'hello\n')
    print(os.path.samefile(source, target))
    try:
        shutil.copyfile(source, source)
    except shutil.SameFileError as exc:
        print(type(exc).__name__)
    print(sorted(shutil.ignore_patterns('*.tmp', 'skip*')(directory, ['a.tmp', 'skipme', 'keep'])))
finally:
    if os.path.exists(source):
        os.unlink(source)
    if os.path.exists(target):
        os.unlink(target)
    os.rmdir(directory)
os.environ['COLUMNS'] = '0'
os.environ['LINES'] = '0'
print(shutil.get_terminal_size(fallback=(91, 37)))
