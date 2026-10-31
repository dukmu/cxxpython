# Private bytecode and extension formats are not CPython's .pyc or C ABI.
import _imp
import importlib.machinery as machinery
from importlib import _bootstrap
import inspect

assert machinery.SOURCE_SUFFIXES is _bootstrap.SOURCE_SUFFIXES
assert machinery.EXTENSION_SUFFIXES is _bootstrap.EXTENSION_SUFFIXES
assert machinery.SOURCE_SUFFIXES == ['.py']
assert machinery.EXTENSION_SUFFIXES == _imp.extension_suffixes()
assert machinery.BYTECODE_SUFFIXES == []
assert machinery.DEBUG_BYTECODE_SUFFIXES is machinery.BYTECODE_SUFFIXES
assert machinery.OPTIMIZED_BYTECODE_SUFFIXES is machinery.BYTECODE_SUFFIXES
assert machinery.all_suffixes() == machinery.SOURCE_SUFFIXES + machinery.EXTENSION_SUFFIXES
for suffix in machinery.all_suffixes():
    assert inspect.getmodulename('sample' + suffix) == 'sample'
finder = machinery.FileFinder('.', (machinery.NativeLoader, machinery.EXTENSION_SUFFIXES),
                              (machinery.SourceFileLoader, machinery.SOURCE_SUFFIXES))
assert [suffix for suffix, loader in finder._loaders] == (
    machinery.EXTENSION_SUFFIXES + machinery.SOURCE_SUFFIXES)
print('suffix metadata matches actual loaders and private extension ABI')
