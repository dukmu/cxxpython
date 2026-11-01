import platform
import sys


implementation = platform.python_implementation()
expected = {'cpython': 'CPython', 'cxxpython': 'cxxpython'}
print(implementation == expected[sys.implementation.name])
print(platform.python_version_tuple() == tuple(str(n) for n in sys.version_info[:3]))
print(platform.python_version() == '.'.join(platform.python_version_tuple()))
print(isinstance(platform.python_branch(), str))
print(isinstance(platform.python_revision(), str))
print(len(platform.python_build()) == 2)
print(isinstance(platform.python_compiler(), str))

# Explicit foreign-version strings retain the fixed upstream parsing behavior.
parsed = platform._sys_version('3.12.14 (main, Oct  1 2026, 12:34:56) [Clang 21.1.8]')
print(parsed[:2])
print(parsed[4:])
try:
    platform._sys_version('not a version')
except ValueError:
    print('invalid version')

print(platform.system_alias('SunOS', '5.10', 'Generic'))
print(platform.system_alias('Linux', '6.1', '#1'))
