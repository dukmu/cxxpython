import importlib.machinery
import importlib.util
import os
import py_compile
import sys

source = 'cache_subject.py'
with open(source, 'w') as stream:
    stream.write('value = 42\n')

for mode in (py_compile.PycInvalidationMode.TIMESTAMP,
             py_compile.PycInvalidationMode.CHECKED_HASH,
             py_compile.PycInvalidationMode.UNCHECKED_HASH):
    cache = py_compile.compile(source, doraise=True, invalidation_mode=mode)
    with open(cache, 'rb') as stream:
        header = stream.read(16)
    print(len(header), header[:4] == importlib.util.MAGIC_NUMBER,
          int.from_bytes(header[4:8], 'little'))
    loader = importlib.machinery.SourceFileLoader('cache_subject', source)
    spec = importlib.util.spec_from_loader('cache_subject', loader)
    module = importlib.util.module_from_spec(spec)
    loader.exec_module(module)
    print(module.value)

print(importlib.util.source_from_cache(cache) == source)
legacy = 'cache_subject.pyc'
py_compile.compile(source, cfile=legacy, doraise=True)
os.unlink(source)
loader = importlib.machinery.SourcelessFileLoader('cache_subject', legacy)
spec = importlib.util.spec_from_file_location('cache_subject', legacy)
print(type(spec.loader).__name__)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
print(module.value)
sys.modules.pop('cache_subject', None)
sys.path.insert(0, os.getcwd())
import cache_subject
print(cache_subject.value)
