import glob
import os

os.mkdir('tree')
os.mkdir('tree/sub')
os.mkdir('tree/.hidden')
for name in ('alpha.txt', 'beta.txt', 'a1.py', '.secret.txt', 'literal[1].txt',
             'sub/nested.txt', '.hidden/inside.txt'):
    fd = os.open('tree/' + name, os.O_CREAT | os.O_WRONLY, 0o600)
    os.close(fd)
os.symlink('sub', 'tree/link')
os.symlink('missing', 'tree/broken')

print('star', sorted(glob.glob('*.txt', root_dir='tree')))
print('question', sorted(glob.iglob('a?.py', root_dir='tree')))
print('bracket', sorted(glob.glob('[ab]*.txt', root_dir='tree')))
print('hidden', sorted(glob.glob('.*', root_dir='tree')))
print('include hidden', sorted(glob.glob('*.txt', root_dir='tree', include_hidden=True)))
print('recursive', sorted(glob.iglob('**/*.txt', root_dir='tree', recursive=True)))
print('recursive hidden', sorted(glob.glob('**/*.txt', root_dir='tree', recursive=True,
                                           include_hidden=True)))
print('escape', glob.escape('literal[1].txt'),
      glob.glob(glob.escape('literal[1].txt'), root_dir='tree'))
print('bytes', sorted(glob.glob(b'*.txt', root_dir=b'tree')))
print('symlinks', glob.glob('broken', root_dir='tree'),
      sorted(glob.glob('link/*.txt', root_dir='tree')))
print('missing', glob.glob('missing/*', root_dir='tree'))
