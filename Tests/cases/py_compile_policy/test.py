import importlib.util
import py_compile

path = importlib.util.cache_from_source('folder/example.py', optimization=2)
print(path.startswith('folder/__pycache__/example.'))
print(path.endswith('.opt-2.pyc'))
print(importlib.util.source_from_cache(path))
try:
    importlib.util.cache_from_source('example.py', optimization='bad-value')
except ValueError:
    print('bad optimization')
try:
    importlib.util.source_from_cache('example.pyc')
except ValueError:
    print('bad cache path')
with open('bad_source.py', 'w') as stream:
    stream.write('if :\n')
try:
    py_compile.compile('bad_source.py', doraise=True)
except py_compile.PyCompileError as error:
    print(error.exc_type_name, error.file)
print(py_compile.compile('bad_source.py', quiet=2) is None)
