"""Loader and specification types provided by this runtime's import policy."""

from ._bootstrap import ModuleSpec, SourceFileLoader, MemorySourceLoader, NativeLoader, NamespaceLoader, RegistryFinder, PathFinder, FileFinder
from _imp import extension_suffixes
