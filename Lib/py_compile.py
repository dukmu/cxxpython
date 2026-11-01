"""Compile Python source to cxxpython's native bytecode cache format.

Adapted from CPython 3.12.14's py_compile.py, distributed under the PSF
License; see doc/licenses/python-3.12.14.txt.
"""

import enum
import importlib._bootstrap_external as _external
import importlib.machinery
import importlib.util
import os
import sys
import traceback

__all__ = ['compile', 'main', 'PyCompileError', 'PycInvalidationMode']


class PyCompileError(Exception):
    def __init__(self, exc_type, exc_value, file, msg=''):
        exc_type_name = exc_type.__name__
        if exc_type is SyntaxError:
            message = ''.join(traceback.format_exception_only(exc_type, exc_value))
            message = message.replace('File "<string>"', 'File "%s"' % file)
        else:
            message = 'Sorry: %s: %s' % (exc_type_name, exc_value)
        self.exc_type_name = exc_type_name
        self.exc_value = exc_value
        self.file = file
        self.msg = msg or message
        Exception.__init__(self, self.msg, exc_type_name, exc_value, file)

    def __str__(self):
        return self.msg


class PycInvalidationMode(enum.Enum):
    TIMESTAMP = 1
    CHECKED_HASH = 2
    UNCHECKED_HASH = 3


def _get_default_invalidation_mode():
    if os.environ.get('SOURCE_DATE_EPOCH'):
        return PycInvalidationMode.CHECKED_HASH
    return PycInvalidationMode.TIMESTAMP


def compile(file, cfile=None, dfile=None, doraise=False, optimize=-1,
            invalidation_mode=None, quiet=0):
    if invalidation_mode is None:
        invalidation_mode = _get_default_invalidation_mode()
    if cfile is None:
        optimization = None if optimize < 0 else (optimize if optimize else '')
        cfile = importlib.util.cache_from_source(file, optimization=optimization)
    if os.path.islink(cfile):
        raise FileExistsError('{} is a symlink and will be changed into a regular file if import writes a byte-compiled file to it'.format(cfile))
    if os.path.exists(cfile) and not os.path.isfile(cfile):
        raise FileExistsError('{} is a non-regular file and will be changed into a regular one if import writes a byte-compiled file to it'.format(cfile))
    loader = importlib.machinery.SourceFileLoader('<py_compile>', file)
    source = loader.get_data(file)
    try:
        code = loader.source_to_code(source, dfile or file, _optimize=optimize)
    except Exception as error:
        py_error = PyCompileError(error.__class__, error, dfile or file)
        if quiet < 2:
            if doraise:
                raise py_error
            sys.stderr.write(py_error.msg + '\n')
        return None
    dirname = os.path.dirname(cfile)
    if dirname:
        os.makedirs(dirname, exist_ok=True)
    if invalidation_mode == PycInvalidationMode.TIMESTAMP:
        stats = loader.path_stats(file)
        data = _external._code_to_timestamp_pyc(code, stats['mtime'], stats['size'])
    else:
        data = _external._code_to_hash_pyc(
            code, importlib.util.source_hash(source),
            invalidation_mode == PycInvalidationMode.CHECKED_HASH)
    _external._write_atomic(cfile, data, _external._calc_mode(file))
    return cfile


def main():
    import argparse
    parser = argparse.ArgumentParser(description='A simple command-line interface for py_compile module.')
    parser.add_argument('-q', '--quiet', action='store_true', help='Suppress error output')
    parser.add_argument('filenames', nargs='+', help='Files to compile')
    args = parser.parse_args()
    filenames = ([line.rstrip('\n') for line in sys.stdin.readlines()]
                 if args.filenames == ['-'] else args.filenames)
    for filename in filenames:
        try:
            compile(filename, doraise=True)
        except (PyCompileError, OSError) as error:
            if args.quiet:
                parser.exit(1)
            parser.exit(1, str(error))


if __name__ == '__main__':
    main()
