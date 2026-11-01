"""Import utilities adapted from CPython 3.12 under the PSF License.

See doc/licenses/python-3.12.14.txt.  find_spec follows CPython's util.py;
relative name resolution uses the string operations available in this runtime.
"""

import sys
import _imp
from ._bootstrap import ModuleSpec, SourceFileLoader, SourcelessFileLoader, NativeLoader, module_from_spec, _find_spec
from ._bootstrap_external import MAGIC_NUMBER, cache_from_source, source_from_cache, source_hash


def resolve_name(name, package):
    if name[:1] != '.':
        return name
    if not package:
        raise ImportError('no package specified for relative import')
    level = 0
    for character in name:
        if character != '.':
            break
        level += 1
    base = package
    remaining = level
    while remaining > 1:
        base, dot, tail = base.rpartition('.')
        if not dot:
            raise ImportError('attempted relative import beyond top-level package')
        remaining -= 1
    suffix = name[level:]
    if suffix:
        return base + '.' + suffix
    return base


def find_spec(name, package=None):
    """Return the spec for the specified module, importing its parent if needed."""
    fullname = resolve_name(name, package) if name[:1] == '.' else name
    if fullname not in sys.modules:
        parent_name = fullname.rpartition('.')[0]
        if parent_name:
            parent = __import__(parent_name, fromlist=['__path__'])
            try:
                parent_path = parent.__path__
            except AttributeError as e:
                raise ModuleNotFoundError(
                    f'__path__ attribute not found on {parent_name!r} '
                    f'while trying to find {fullname!r}', name=fullname) from e
        else:
            parent_path = None
        return _find_spec(fullname, parent_path)
    else:
        module = sys.modules[fullname]
        if module is None:
            return None
        try:
            spec = module.__spec__
        except AttributeError:
            raise ValueError(f'{name}.__spec__ is not set') from None
        else:
            if spec is None:
                raise ValueError(f'{name}.__spec__ is None')
            return spec


_POPULATE = object()


def spec_from_file_location(name, location=None, *, loader=None,
                            submodule_search_locations=_POPULATE):
    """Create a source or native extension spec, or use the supplied loader."""
    if location is None:
        if loader is None or not hasattr(loader, 'get_filename'):
            return None
        location = loader.get_filename(name)
    location = _imp._path_absolute(location)
    if loader is None:
        if location[-9:] == '.cxxpy.so':
            loader = NativeLoader(name, location)
        elif location[-3:] == '.py':
            loader = SourceFileLoader(name, location)
        elif location[-4:] == '.pyc':
            loader = SourcelessFileLoader(name, location)
        else:
            return None
    if submodule_search_locations is _POPULATE:
        is_package = False
        if hasattr(loader, 'is_package'):
            try:
                is_package = loader.is_package(name)
            except ImportError:
                pass
        locations = [] if is_package else None
    else:
        locations = submodule_search_locations
    if locations is not None and not locations:
        locations.append(location.rpartition('/')[0])
    spec = ModuleSpec(name, loader, origin=location, is_package=locations is not None)
    spec.submodule_search_locations = locations
    spec.has_location = True
    return spec


def spec_from_loader(name, loader, *, origin=None, is_package=None):
    if origin is None:
        origin = getattr(loader, '_ORIGIN', None)
    if not origin and hasattr(loader, 'get_filename'):
        if is_package is None:
            return spec_from_file_location(name, loader=loader)
        locations = [] if is_package else None
        return spec_from_file_location(name, loader=loader,
                                       submodule_search_locations=locations)
    if is_package is None and hasattr(loader, 'is_package'):
        try:
            is_package = loader.is_package(name)
        except ImportError:
            pass
    return ModuleSpec(name, loader, origin=origin, is_package=is_package)
