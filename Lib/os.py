"""Expose portable operating-system interfaces on top of the POSIX substrate."""

import posix
import posixpath as path

__all__ = [
    'name',
    'sep',
    'linesep',
    'curdir',
    'pardir',
    'pathsep',
    'path',
    'environ',
    'getcwd',
    'listdir',
]

name = 'posix'
sep = '/'
linesep = '\n'
curdir = '.'
pardir = '..'
pathsep = ':'
environ = posix.environ()
getcwd = posix.getcwd
listdir = posix.listdir
