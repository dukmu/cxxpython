"""Python import policy backed by the runtime's native import primitives."""

import _imp
from . import _bootstrap


def import_module(name, package=None):
    level = 0
    if name[:1] == '.':
        if not package:
            raise TypeError('the package argument is required for a relative import')
        for character in name:
            if character != '.':
                break
            level += 1
    return _imp._gcd_import(name[level:], package, level)


def reload(module):
    try:
        name = module.__spec__.name
    except AttributeError:
        try:
            name = module.__name__
        except AttributeError:
            raise TypeError('reload() argument must be a module')
    return _imp._reload(module, name)
