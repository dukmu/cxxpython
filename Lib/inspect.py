"""Introspection helpers for live objects, functions, frames, and code."""

import sys

__all__ = [
    'currentframe',
    'getdoc',
    'isframe',
    'iscode',
    'isfunction',
    'signature',
    'getsourcefile',
    'getfile',
]

_frame_type = type(sys._getframe())
_code_type = type(sys._getframe().f_code)

def currentframe():
    "Return the caller's frame object."
    return sys._getframe(1)

def isframe(object):
    "Return True if the object is a frame."
    return type(object) is _frame_type

def iscode(object):
    "Return True if the object is a code object."
    return type(object) is _code_type

def isfunction(object):
    "Return True if the object is a Python function."
    return type(object).__name__ == 'function'

def getsourcefile(object):
    "Return the filename associated with an object."
    if isframe(object):
        return object.f_code.co_filename
    if iscode(object):
        return object.co_filename
    return object.__code__.co_filename

def getfile(object):
    "Return the filename associated with an object."
    return getsourcefile(object)

def getdoc(object):
    "Return an object's docstring or None."
    try:
        return object.__doc__
    except AttributeError:
        return None

def signature(object):
    "Return an object's text signature or None."
    try:
        return object.__text_signature__
    except AttributeError:
        return None
