"""Native bytecode cache helpers, adapted from CPython 3.12 importlib.

CPython is distributed under the PSF License; see
doc/licenses/python-3.12.14.txt.  The magic and marshal payload are native to
cxxpython and are not interchangeable with CPython bytecode.
"""

import sys

MAGIC_NUMBER = b'CX\r\n'
_CACHE_TAG = 'cxxpython-native1'


def cache_from_source(path, debug_override=None, *, optimization=None):
    import os
    path = os.fspath(path)
    if debug_override is not None:
        if optimization is not None:
            raise TypeError('debug_override or optimization must be set to None')
        optimization = '' if debug_override else '1'
    if optimization is None:
        level = getattr(getattr(sys, 'flags', None), 'optimize', 0)
        optimization = str(level) if level else ''
    else:
        optimization = str(optimization)
    if optimization and not optimization.isalnum():
        raise ValueError('optimization must be alphanumeric')
    head, tail = os.path.split(path)
    base, sep, extension = tail.rpartition('.')
    if not sep:
        base = tail
    tag = _CACHE_TAG
    if optimization:
        tag += '.opt-' + optimization
    return os.path.join(head, '__pycache__', base + '.' + tag + '.pyc')


def source_from_cache(path):
    import os
    path = os.fspath(path)
    head, tail = os.path.split(path)
    parent, cache = os.path.split(head)
    if cache != '__pycache__':
        raise ValueError('__pycache__ not bottom-level directory')
    parts = tail.split('.')
    if len(parts) not in (3, 4) or parts[-1] != 'pyc':
        raise ValueError('expected only 2 or 3 dots in cache filename')
    if len(parts) == 4 and (not parts[2].startswith('opt-') or
                            not parts[2][4:].isalnum()):
        raise ValueError('invalid optimization component')
    return os.path.join(parent, parts[0] + '.py')


def source_hash(source_bytes):
    import _imp
    return _imp.source_hash(int.from_bytes(MAGIC_NUMBER, 'little'), source_bytes)


def _pack_uint32(value):
    return (int(value) & 0xffffffff).to_bytes(4, 'little')


def _code_to_timestamp_pyc(code, mtime=0, source_size=0):
    import marshal
    return (MAGIC_NUMBER + _pack_uint32(0) + _pack_uint32(mtime) +
            _pack_uint32(source_size) + marshal.dumps(code))


def _code_to_hash_pyc(code, source_hash, checked=True):
    import marshal
    if len(source_hash) != 8:
        raise ValueError('source hash must be 8 bytes')
    return MAGIC_NUMBER + _pack_uint32(3 if checked else 1) + source_hash + marshal.dumps(code)


def _classify_pyc(data, name):
    if len(data) < 16:
        raise EOFError('truncated bytecode header for ' + name)
    if data[:4] != MAGIC_NUMBER:
        raise ImportError('bad magic number in ' + name)
    flags = int.from_bytes(data[4:8], 'little')
    if flags & ~3 or flags == 2:
        raise ImportError('invalid flags in ' + name)
    return flags


def _compile_bytecode(data, name):
    import marshal
    code = marshal.loads(data[16:])
    if not isinstance(code, type(_compile_bytecode.__code__)):
        raise ImportError('non-code object in ' + name)
    return code


def _calc_mode(path):
    import os
    try:
        return os.stat(path).st_mode | 0o200
    except OSError:
        return 0o666


def _write_atomic(path, data, mode=0o666):
    import os
    temporary = path + '.' + str(id(data))
    fd = os.open(temporary, os.O_EXCL | os.O_CREAT | os.O_WRONLY, mode & 0o666)
    try:
        with os.fdopen(fd, 'wb') as stream:
            stream.write(data)
        os.replace(temporary, path)
    except BaseException:
        try:
            os.unlink(temporary)
        except OSError:
            pass
        raise
