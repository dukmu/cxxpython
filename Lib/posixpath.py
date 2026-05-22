"""Common pathname manipulations for POSIX paths."""

sep = '/'
altsep = None
curdir = '.'
pardir = '..'
extsep = '.'
pathsep = ':'
defpath = '/bin:/usr/bin'
devnull = '/dev/null'

__all__ = [
    'sep',
    'altsep',
    'curdir',
    'pardir',
    'extsep',
    'pathsep',
    'defpath',
    'devnull',
    'join',
    'basename',
    'dirname',
    'split',
    'isabs',
]


def _as_path_text(path):
    """Return path as a string or raise TypeError."""
    if isinstance(path, str):
        return path
    raise TypeError('path must be str')


def join(path, *paths):
    """Join one or more path components using POSIX separators."""
    result = _as_path_text(path)
    for item in paths:
        component = _as_path_text(item)
        absolute = False
        if len(component) > 0:
            if component[0] == sep:
                absolute = True
        if absolute:
            result = component
        else:
            if result == '':
                result = result + component
            else:
                if result[-1] == sep:
                    result = result + component
                else:
                    result = result + sep + component
    return result


def basename(path):
    """Return the final component of a POSIX path."""
    path = _as_path_text(path)
    last = -1
    for index in range(len(path)):
        if path[index] == sep:
            last = index
    return path[last + 1:]


def dirname(path):
    """Return the directory component of a POSIX path."""
    path = _as_path_text(path)
    last = -1
    for index in range(len(path)):
        if path[index] == sep:
            last = index
    if last < 0:
        return ''
    if last == 0:
        return sep
    return path[:last]


def split(path):
    """Split a POSIX path into (head, tail)."""
    return (dirname(path), basename(path))


def isabs(path):
    """Return whether a POSIX path is absolute."""
    path = _as_path_text(path)
    if len(path) == 0:
        return False
    return path[0] == sep
