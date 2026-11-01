"""Loader and specification types provided by this runtime's import policy."""

from ._bootstrap import ModuleSpec, SourceFileLoader, SourcelessFileLoader, MemorySourceLoader, NativeLoader, NamespaceLoader, RegistryFinder, PathFinder, FileFinder
from ._bootstrap import (SOURCE_SUFFIXES, BYTECODE_SUFFIXES, DEBUG_BYTECODE_SUFFIXES,
                         OPTIMIZED_BYTECODE_SUFFIXES, EXTENSION_SUFFIXES)
from _imp import extension_suffixes


def all_suffixes():
    """Return recognized source, bytecode and native module suffixes."""
    return SOURCE_SUFFIXES + BYTECODE_SUFFIXES + EXTENSION_SUFFIXES
