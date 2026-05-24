"""Regular expression operations built on the native _re substrate."""

import _re

__all__ = ['Pattern', 'Match', 'compile', 'match', 'search']

Pattern = _re.Pattern
Match = _re.Match


def compile(pattern, flags=0):
    return _re.compile(pattern, flags)


def match(pattern, string, flags=0):
    return _re.match(pattern, string, flags)


def search(pattern, string, flags=0):
    return _re.search(pattern, string, flags)
