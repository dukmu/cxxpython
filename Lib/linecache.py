"""Cache source lines from files for traceback and debugging tools."""

import io

__all__ = [
    'getline',
    'getlines',
    'clearcache',
    'checkcache',
    'lazycache',
    'updatecache',
]

cache = {}


def _split_lines(text):
    lines = []
    start = 0
    for index in range(len(text)):
        if text[index] == '\n':
            lines.append(text[start:index + 1])
            start = index + 1
    if start < len(text):
        lines.append(text[start:])
    return lines


def getline(filename, lineno, module_globals=None):
    """Get a line for a file, returning an empty string on failure."""
    if lineno < 1:
        return ''
    lines = getlines(filename, module_globals)
    if lineno > len(lines):
        return ''
    return lines[lineno - 1]


def getlines(filename, module_globals=None):
    """Get all cached lines for a file, loading them if necessary."""
    if filename in cache:
        return cache[filename]
    return updatecache(filename, module_globals)


def clearcache():
    """Clear the source line cache."""
    global cache
    cache = {}


def checkcache(filename=None):
    """Discard cached source lines.

    cxxpython does not expose POSIX stat metadata yet, so freshness cannot be
    checked precisely.  The conservative behavior is to invalidate the
    requested cache entry, or the whole cache when no filename is supplied.
    """
    if filename is None:
        clearcache()
        return
    if filename in cache:
        del cache[filename]


def updatecache(filename, module_globals=None):
    """Update the cache for a file and return its lines."""
    if not isinstance(filename, str):
        return []
    try:
        stream = io.open(filename, 'r')
    except OSError:
        return []
    try:
        text = stream.read()
    finally:
        stream.close()
    lines = _split_lines(text)
    cache[filename] = lines
    return lines


def lazycache(filename, module_globals):
    """Seed a lazy cache entry if loader support is available.

    Loader-backed lazy source retrieval needs importlib loader protocol
    support.  Until that substrate exists, return False like CPython does when
    no lazy entry can be registered.
    """
    return False
