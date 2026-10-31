import gc
import os

with open('access-file', 'w') as file:
    file.write('data')
print('access', os.access('access-file', os.F_OK),
      os.access('access-file', os.R_OK), os.access('access-file', os.X_OK))
print('missing', os.access('absent', os.F_OK))
os.symlink('absent', 'access-link')
print('symlink', os.access('access-link', os.F_OK),
      os.access('access-link', os.F_OK, follow_symlinks=False))
directory = os.open('.', os.O_RDONLY)
try:
    print('dir fd', os.access('access-file', os.F_OK, dir_fd=directory),
          os.access('access-file', os.R_OK, dir_fd=directory, effective_ids=True))
finally:
    os.close(directory)

class Path:
    def __fspath__(self):
        gc.collect()
        return 'access-file'

print('path protocol', os.access(Path(), os.F_OK))
print('capabilities', os.access in os.supports_dir_fd,
      os.access in os.supports_effective_ids,
      os.access in os.supports_follow_symlinks)
