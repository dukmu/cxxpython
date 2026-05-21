import sys
import os

print(sys.name)
print(sys.version)
print(sys.argv[0].__contains__('cxxpython'))
print(type(sys.path))
print(type(sys.modules))
print(type(sys.builtin_module_names))
print(sys.builtin_module_names.__contains__('posix'))
print(sys.builtin_module_names.__contains__('_posix'))
print(sys.builtin_module_names.__contains__('sys'))
print(sys.builtin_module_names.__contains__('_sys'))
print(sys.builtin_module_names.__contains__('gc'))
print(sys.builtin_module_names.__contains__('_gc'))
print(type(sys.stdin))
print(type(sys.stdout))
print(type(sys.stderr))
print(os.name)
print(os.sep)
print(os.linesep == '\n')
print(os.curdir)
print(os.pardir)
cwd = os.getcwd()
print(type(cwd))
print(len(cwd) > 0)
entries = os.listdir(os.curdir)
print(type(entries))
print(len(entries) > 0)
environment = os.environ
print(type(environment))
try:
    os.listdir(42)
except TypeError as exc:
    print(type(exc))
try:
    os.listdir('/definitely/not/a/cxxpython/path')
except OSError as exc:
    print(type(exc))
