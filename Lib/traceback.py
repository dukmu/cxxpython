"""Format and print stack traces.

The runtime currently exposes ``BaseException.__traceback__`` as a list of
``(filename, line, name)`` tuples.  This module is the script-level policy
layer over that seed; the object representation can grow later without
requiring the CLI or REPL to own traceback formatting.
"""

import sys
import linecache

__all__ = ['format_exception', 'format_tb', 'print_exception', 'print_tb']


def _exception_type_name(exc):
    cls = exc.__class__
    try:
        return cls.__name__
    except AttributeError:
        return cls.__qualname__


def _exception_message(exc):
    args = exc.args
    if len(args) == 0:
        return ''
    if len(args) == 1:
        return str(args[0])
    return str(args)


def _format_entry(entry):
    result = '  File "' + entry[0] + '", line ' + str(entry[1]) + ', in ' + entry[2] + '\n'
    source = linecache.getline(entry[0], entry[1])
    if source:
        result = result + '    ' + source.strip() + '\n'
    return result


def format_tb(tb):
    """Return a list of strings for traceback entries."""
    lines = []
    for entry in reversed(tb):
        lines.append(_format_entry(entry))
    return lines


def print_tb(tb, file=None):
    """Print traceback entries to a file-like object."""
    if file is None:
        file = sys.stderr
    for line in format_tb(tb):
        file.write(line)
    file.flush()


def format_exception(exc, tb=None):
    """Return a list of strings describing an exception and traceback."""
    lines = []
    if tb is None:
        tb = exc.__traceback__
    if len(tb) != 0:
        lines.append('Traceback (most recent call last):\n')
        for line in format_tb(tb):
            lines.append(line)
    message = _exception_message(exc)
    if message:
        lines.append(_exception_type_name(exc) + ': ' + message + '\n')
    else:
        lines.append(_exception_type_name(exc) + '\n')
    return lines


def print_exception(exc, file=None, tb=None):
    """Print exception information to a file-like object."""
    if file is None:
        file = sys.stderr
    file.write(''.join(format_exception(exc, tb)))
    file.flush()
