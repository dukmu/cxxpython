"""Minimal import policy, adapted from CPython 3.12's importlib._bootstrap.

CPython is distributed under the Python Software Foundation License; see
doc/licenses/python-3.12.14.txt.

Loader callbacks take a module for execution, or a module name for source and
code retrieval.  A missing code callback falls back to compiling source.

_find_spec and module_from_spec follow CPython's _bootstrap.py; PathFinder
and _NamespacePath follow _bootstrap_external.py.  Snapshots and namespace
state publication replace the import lock and mutable multi-field state.
Warnings, verbose tracing and importlib.metadata integration are not yet
available.  FileFinder uses uncached native file probes instead of CPython's
listdir/stat/set-based cache; its complete upstream API is not yet provided.
"""

import sys
import _imp
import builtins

SOURCE_SUFFIXES = ['.py']
BYTECODE_SUFFIXES = ['.pyc']
DEBUG_BYTECODE_SUFFIXES = BYTECODE_SUFFIXES
OPTIMIZED_BYTECODE_SUFFIXES = BYTECODE_SUFFIXES
EXTENSION_SUFFIXES = _imp.extension_suffixes()


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
        if self._cached is None and self._set_fileattr and self.origin:
            if self.origin.endswith('.py'):
                external = sys.modules.get('importlib._bootstrap_external')
                os = sys.modules.get('os')
                if (external is not None and hasattr(external, 'cache_from_source') and
                        os is not None and hasattr(os, 'path')):
                    self._cached = external.cache_from_source(self.origin)
            elif self.origin.endswith('.pyc'):
                self._cached = self.origin
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

    def get_data(self, path):
        with open(path, 'rb') as stream:
            return stream.read()

    def path_stats(self, path):
        import os
        stat = os.stat(path)
        return {'mtime': stat.st_mtime, 'size': stat.st_size}

    def source_to_code(self, data, path, *, _optimize=-1):
        return builtins.compile(data, path, 'exec', dont_inherit=True, optimize=_optimize)

    def get_code(self, fullname):
        self.get_filename(fullname)
        if self._get_code_callback is not None:
            return self._get_code_callback(fullname)
        # os and the cache helpers themselves must be importable before this
        # policy can use their filesystem and marshal dependencies.
        os = sys.modules.get('os')
        if (fullname == 'importlib._bootstrap_external' or os is None or
                not hasattr(os, 'path') or not hasattr(os, 'stat')):
            return _SourceLoader.get_code(self, fullname)
        from . import _bootstrap_external as external
        source = self.get_data(self.path)
        stats = self.path_stats(self.path)
        cache = external.cache_from_source(self.path)
        try:
            data = self.get_data(cache)
            flags = external._classify_pyc(data, fullname)
            if flags & 1:
                valid = not flags & 2 or data[8:16] == external.source_hash(source)
            else:
                valid = (data[8:12] == external._pack_uint32(stats['mtime']) and
                         data[12:16] == external._pack_uint32(stats['size']))
            if valid:
                return external._compile_bytecode(data, fullname)
        except (OSError, ImportError, EOFError, ValueError, TypeError):
            pass
        code = self.source_to_code(source, self.path)
        if not getattr(sys, 'dont_write_bytecode', False):
            try:
                os.makedirs(os.path.dirname(cache), exist_ok=True)
                data = external._code_to_timestamp_pyc(code, stats['mtime'], stats['size'])
                external._write_atomic(cache, data, external._calc_mode(self.path))
            except (OSError, ImportError, TypeError, ValueError):
                pass
        return code


class SourcelessFileLoader(_SourceLoader):
    def is_package(self, fullname):
        self.get_filename(fullname)
        return self.path.rpartition('/')[2] == '__init__.pyc'

    def get_code(self, fullname):
        self.get_filename(fullname)
        from . import _bootstrap_external as external
        with open(self.path, 'rb') as stream:
            data = stream.read()
        external._classify_pyc(data, fullname)
        return external._compile_bytecode(data, fullname)


class MemorySourceLoader(_SourceLoader):
    """Loader for registered in-memory source."""


class NativeLoader:
    def __init__(self, name, origin, exec_callback=None):
        self.name = name
        self.path = origin
        self._exec_callback = exec_callback

    def create_module(self, spec):
        return _imp._create_builtin(self.name)

    def get_filename(self, fullname):
        if fullname != self.name:
            raise ImportError('loader cannot handle ' + fullname)
        return self.path

    def is_package(self, fullname):
        path = self.get_filename(fullname)
        return getattr(self, '_is_package', False) or (
            path is not None and path.rpartition('/')[2] == '__init__.cxxpy.so')

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
            if self.path is None or self.path == 'built-in':
                raise ImportError('no native initializer for ' + self.name)
            data = _imp._extension_data(self.name, self.path)
            if data is None or data[3] is None:
                raise ImportError('no native initializer for ' + self.name)
            self._exec_callback = data[3]
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
    else:
        raise ValueError('unknown loader kind')
    if loader is not None:
        loader._is_package = paths is not None
    spec = ModuleSpec(name, loader, origin=origin, is_package=paths is not None)
    spec.submodule_search_locations = paths
    spec.has_location = has_location
    return spec


def _init_module_attrs(spec, module, override=False):
    if spec.loader is None and spec.submodule_search_locations is not None:
        spec.loader = NamespaceLoader(spec.name, spec.submodule_search_locations)
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
        # If create_module() returns None, use default module creation.
        module = spec.loader.create_module(spec)
    elif hasattr(spec.loader, 'exec_module'):
        raise ImportError('loaders that define exec_module() must also define create_module()')
    if module is None:
        module = type(sys)(spec.name)
    _init_module_attrs(spec, module)
    return module


def _find_spec(name, path, target=None):
    """Find a module's spec."""
    meta_path = sys.meta_path
    if meta_path is None:
        raise ImportError('sys.meta_path is None, Python is likely shutting down')
    # CPython warns when meta_path is empty; warnings is not yet available.
    is_reload = name in sys.modules
    for finder in tuple(meta_path):
        try:
            find_spec = finder.find_spec
        except AttributeError:
            continue
        else:
            spec = find_spec(name, path, target)
        if spec is not None:
            if not is_reload and name in sys.modules:
                module = sys.modules[name]
                try:
                    __spec__ = module.__spec__
                except AttributeError:
                    return spec
                else:
                    if __spec__ is None:
                        return spec
                    else:
                        return __spec__
            else:
                return spec
    else:
        return None


def _exec(spec, module):
    if spec.loader is None:
        if spec.submodule_search_locations is None:
            raise ImportError('missing loader')
    else:
        spec.loader.exec_module(module)
    return module


class RegistryFinder:
    @classmethod
    def find_spec(cls, fullname, path=None, target=None):
        data = _imp._registered_data(fullname)
        if data is None:
            return None
        return _make_spec(data)


class FileFinder:
    """Filesystem finder using the supplied loader factories and suffixes."""
    def __init__(self, path, *loader_details):
        loaders = []
        for loader, suffixes in loader_details:
            for suffix in suffixes:
                loaders.append((suffix, loader))
        self._loaders = tuple(loaders)
        self.path = _imp._path_absolute('' if not path or path == '.' else path)

    def find_spec(self, fullname, target=None):
        tail = fullname.rpartition('.')[2]
        base = self.path + '/' + tail
        is_directory = _imp._is_directory(base)
        if is_directory:
            for suffix, factory in self._loaders:
                filename = base + '/__init__' + suffix
                if _imp._is_file(filename):
                    loader = factory(fullname, filename)
                    spec = ModuleSpec(fullname, loader, origin=filename, is_package=True)
                    spec.submodule_search_locations = [base]
                    spec.has_location = True
                    return spec
        for suffix, factory in self._loaders:
            filename = base + suffix
            if _imp._is_file(filename):
                loader = factory(fullname, filename)
                spec = ModuleSpec(fullname, loader, origin=filename)
                spec.has_location = True
                return spec
        if is_directory:
            spec = ModuleSpec(fullname, None, is_package=True)
            spec.submodule_search_locations = [base]
            return spec
        return None

    def invalidate_caches(self):
        pass

    @classmethod
    def path_hook(cls, *loader_details):
        def hook(path):
            if not _imp._is_directory(path):
                raise ImportError('path is not a directory')
            return cls(path, *loader_details)
        return hook


class _NamespacePath:
    _epoch = object()

    def __init__(self, name, path, path_finder):
        self._name = name
        self._path_finder = path_finder
        self._state = (tuple(self._get_parent_path()), self._epoch, path)

    def _get_parent_path(self):
        parent = self._name.rpartition('.')[0]
        if not parent:
            return sys.path
        return sys.modules[parent].__path__

    def _recalculate(self):
        parent = tuple(self._get_parent_path())
        epoch = self._epoch
        state = self._state
        if parent != state[0] or epoch is not state[1]:
            spec = self._path_finder(self._name, parent)
            path = state[2]
            if spec is not None and spec.loader is None and spec.submodule_search_locations:
                path = spec.submodule_search_locations
            state = (parent, epoch, path)
            self._state = state
        return state[2]

    def __iter__(self):
        return iter(self._recalculate())

    def __len__(self):
        return len(self._recalculate())

    def __getitem__(self, index):
        return self._recalculate()[index]

    def __setitem__(self, index, path):
        self._state[2][index] = path

    def __contains__(self, item):
        return item in self._recalculate()

    def __repr__(self):
        return f'_NamespacePath({self._state[2]!r})'

    def append(self, item):
        self._state[2].append(item)


class PathFinder:
    @staticmethod
    def invalidate_caches():
        for path, finder in tuple(sys.path_importer_cache.copy().items()):
            if finder is None or path[:1] != '/':
                del sys.path_importer_cache[path]
            elif hasattr(finder, 'invalidate_caches'):
                finder.invalidate_caches()
        _NamespacePath._epoch = object()

    @staticmethod
    def _path_hooks(path):
        """Search sys.path_hooks for a finder for 'path'."""
        # CPython warns for an empty hook list; warnings is not yet available.
        for hook in tuple(sys.path_hooks):
            try:
                return hook(path)
            except ImportError:
                continue
        else:
            return None

    @classmethod
    def _path_importer_cache(cls, path):
        """Get the finder for a path entry, caching a missing finder as None."""
        if path == '':
            try:
                path = _imp._path_absolute(path)
            except FileNotFoundError:
                return None
        try:
            finder = sys.path_importer_cache[path]
        except KeyError:
            finder = cls._path_hooks(path)
            sys.path_importer_cache[path] = finder
        return finder

    @classmethod
    def _get_spec(cls, fullname, path, target=None):
        """Find the loader or namespace_path for this module/package name."""
        namespace_path = []
        for entry in tuple(path):
            if not isinstance(entry, str):
                continue
            finder = cls._path_importer_cache(entry)
            if finder is not None:
                spec = finder.find_spec(fullname, target)
                if spec is None:
                    continue
                if spec.loader is not None:
                    return spec
                portions = spec.submodule_search_locations
                if portions is None:
                    raise ImportError('spec missing loader')
                namespace_path.extend(portions)
        else:
            spec = ModuleSpec(fullname, None)
            spec.submodule_search_locations = namespace_path
            return spec

    @classmethod
    def find_spec(cls, fullname, path=None, target=None):
        if path is None:
            path = sys.path
        spec = cls._get_spec(fullname, path, target)
        if spec is None:
            return None
        elif spec.loader is None:
            namespace_path = spec.submodule_search_locations
            if namespace_path:
                spec.origin = None
                spec.submodule_search_locations = _NamespacePath(
                    fullname, namespace_path, cls._get_spec)
                return spec
            else:
                return None
        else:
            return spec


_installed = False


def _install():
    global _installed
    if _installed:
        return
    # Only the cold bootstrap owner runs this. Prepare everything before
    # publishing, and overwrite partial installation on a failed-boot retry.
    meta_path = [RegistryFinder, PathFinder]
    path_hooks = [FileFinder.path_hook(
        (NativeLoader, EXTENSION_SUFFIXES), (SourceFileLoader, SOURCE_SUFFIXES),
        (SourcelessFileLoader, BYTECODE_SUFFIXES))]
    cache = {}
    sys.meta_path = meta_path
    sys.path_hooks = path_hooks
    sys.path_importer_cache = cache
    _installed = True
