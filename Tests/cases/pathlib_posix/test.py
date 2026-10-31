from pathlib import Path


root = Path('tree')
root.mkdir()
(root / 'sub').mkdir()
file = root / 'sub' / 'data.txt'
print(file.write_text('hello\n', encoding='utf-8'))
print(repr(file.read_text(encoding='utf-8')))
print(file.exists(), file.is_file(), file.parent.is_dir())
print(sorted(str(path) for path in root.glob('**/*.txt')))
link = root / 'link'
link.symlink_to('sub/data.txt')
print(str(link.readlink()), link.is_symlink(), link.read_text())
renamed = file.rename(root / 'sub' / 'renamed.txt')
print(str(renamed), renamed.exists(), file.exists())
link.unlink()
renamed.unlink()
(root / 'sub').rmdir()
root.rmdir()
