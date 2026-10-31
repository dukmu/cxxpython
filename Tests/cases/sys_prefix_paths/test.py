import os
import sys

for name in ('prefix', 'base_prefix', 'exec_prefix', 'base_exec_prefix', '_stdlib_dir'):
    value = getattr(sys, name)
    print(name, isinstance(value, str), os.path.isabs(value))
print('no venv', sys.prefix == sys.base_prefix,
      sys.exec_prefix == sys.base_exec_prefix)
print('actual stdlib', os.path.isfile(os.path.join(sys._stdlib_dir, 'os.py')))
