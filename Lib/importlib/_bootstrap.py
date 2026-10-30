"""Minimal import policy, adapted from CPython 3.12's importlib._bootstrap.

CPython is distributed under the Python Software Foundation License; see
doc/licenses/python-3.12.14.txt.  Bytecode cache paths are not inferred:
``cached`` stays None unless explicitly assigned.

Loader callbacks take a module for execution, or a module name for source and
code retrieval.  A missing code callback falls back to compiling source.
"""

import sys
import _imp
import builtins


class ModuleSpec:
    def __init__(self, name, loader, *, origin=None, loader_state=None, is_package=None):
        self.name = name
        self.loader = loader
        self.origin = origin
        self.loader_state = loader_state
        self.submodule_search_locations = [] if is_package else None
        self._uninitialized_submodules = []
        self._set_fileattr = False
        self._cached = None

    def __repr__(self):
        args = [f'name={self.name!r}', f'loader={self.loader!r}']
        if self.origin is not None:
            args.append(f'origin={self.origin!r}')
        if self.submodule_search_locations is not None:
            args.append(f'submodule_search_locations={self.submodule_search_locations}')
        return f'{self.__class__.__name__}({", ".join(args)})'

    def __eq__(self, other):
        try:
            return (self.name == other.name and self.loader == other.loader and
                    self.origin == other.origin and
                    self.submodule_search_locations == other.submodule_search_locations and
                    self.cached == other.cached and self.has_location == other.has_location)
        except AttributeError:
            return NotImplemented

    @property
    def parent(self):
        if self.submodule_search_locations is None:
            return self.name.rpartition('.')[0]
        return self.name

    @property
    def has_location(self):
        return self._set_fileattr

    @has_location.setter
    def has_location(self, value):
        self._set_fileattr = bool(value)

    @property
    def cached(self):
        return self._cached

    @cached.setter
    def cached(self, value):
        self._cached = value


class _SourceLoader:
    def __init__(self, name, origin, exec_callback=None, get_source_callback=None,
                 get_code_callback=None):
        self.name = name
        self.path = origin
        self._exec_callback = exec_callback
        self._get_source_callback = get_source_callback
        self._get_code_callback = get_code_callback
        self._is_package = False

    def create_module(self, spec):
        return None

    def get_filename(self, fullname):
        if fullname != self.name:
            raise ImportError('loader cannot handle ' + fullname)
        return self.path

    def get_source(self, fullname):
        self.get_filename(fullname)
        if self._get_source_callback is None:
            return None
        return self._get_source_callback(fullname)

    def is_package(self, fullname):
        self.get_filename(fullname)
        return self._is_package

    def get_code(self, fullname):
        self.get_filename(fullname)
        if self._get_code_callback is not None:
            return self._get_code_callback(fullname)
        source = self.get_source(fullname)
        if source is None:
            return None
        return _imp.source_to_code(source, self.path)

    def exec_module(self, module):
        if self._exec_callback is not None:
            self._exec_callback(module)
            return
        code = self.get_code(module.__name__)
        if code is None:
            raise ImportError('no code for ' + module.__name__)
        _imp.exec_code(code, module)


class SourceFileLoader(_SourceLoader):
    """Source file loader; source access is supplied by the native callback."""

    def get_source(self, fullname):
        self.get_filename(fullname)
        if self._get_source_callback is None:
            return _imp._get_source(self.path)
        return self._get_source_callback(fullname)

    def is_package(self, fullname):
        self.get_filename(fullname)
        return self.path.rpartition('/')[2] == '__init__.py'


class MemorySourceLoader(_SourceLoader):
    """Loader for registered in-memory source."""


class NativeLoader:
    def __init__(self, name, origin, exec_callback=None):
        self.name = name
        self.path = origin
        self._exec_callback = exec_callback

    def create_module(self, spec):
        return None

    def get_source(self, fullname):
        if fullname != self.name:
            raise ImportError('loader cannot handle ' + fullname)
        return None

    def get_code(self, fullname):
        if fullname != self.name:
            raise ImportError('loader cannot handle ' + fullname)
        return None

    def exec_module(self, module):
        if self._exec_callback is None:
            raise ImportError('no native initializer for ' + self.name)
        self._exec_callback(module)


class NamespaceLoader:
    def __init__(self, name, path):
        self.name = name
        self._path = path

    def create_module(self, spec):
        return None

    def exec_module(self, module):
        pass

    def is_package(self, fullname):
        if fullname != self.name:
            raise ImportError('loader cannot handle ' + fullname)
        return True

    def get_source(self, fullname):
        self.is_package(fullname)
        return ''

    def get_code(self, fullname):
        self.is_package(fullname)
        return _imp.source_to_code('', '<string>')


def _make_spec(data):
    name, origin, paths, execute, get_source, get_code, kind, has_location = data
    if kind == 0:
        loader = SourceFileLoader(name, origin, execute, get_source, get_code)
    elif kind == 1:
        loader = MemorySourceLoader(name, origin, execute, get_source, get_code)
    elif kind == 2:
        loader = NativeLoader(name, origin, execute)
    elif kind == 3:
        loader = NamespaceLoader(name, paths)
    else:
        raise ValueError('unknown loader kind')
    loader._is_package = paths is not None
    spec = ModuleSpec(name, loader, origin=origin, is_package=paths is not None)
    spec.submodule_search_locations = paths
    spec.has_location = has_location
    return spec


def _init_module_attrs(spec, module, override=False):
    if override or getattr(module, '__name__', None) is None:
        module.__name__ = spec.name
    if override or getattr(module, '__loader__', None) is None:
        module.__loader__ = spec.loader
    if override or getattr(module, '__package__', None) is None:
        module.__package__ = spec.parent
    module.__spec__ = spec
    if spec.submodule_search_locations is not None:
        if override or getattr(module, '__path__', None) is None:
            module.__path__ = spec.submodule_search_locations
        if isinstance(spec.loader, NamespaceLoader) and not spec.has_location:
            module.__file__ = None
    if spec.has_location:
        if override or getattr(module, '__file__', None) is None:
            module.__file__ = spec.origin
        if override or getattr(module, '__cached__', None) is None:
            module.__cached__ = spec.cached
    return module


def module_from_spec(spec):
    module = None
    if hasattr(spec.loader, 'create_module'):
        module = spec.loader.create_module(spec)
    elif hasattr(spec.loader, 'exec_module'):
        raise ImportError('loaders that define exec_module() must also define create_module()')
    if module is None:
        module = type(sys)(spec.name)
    return _init_module_attrs(spec, module)
