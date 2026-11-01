import gc
import math
import struct
import sys
import warnings


class FloatOverride(float):
    def __float__(self):
        raise RuntimeError('float override called')


class IntOverride(int):
    def __float__(self):
        gc.collect()
        return 4.5


class IntIndex(int):
    def __index__(self):
        raise RuntimeError('index override called')


class IndexOnly:
    def __index__(self):
        gc.collect()
        return 7


class IndexSubclass:
    def __index__(self):
        gc.collect()
        return IntIndex(11)


class BadFloat:
    def __float__(self):
        return 3


class StringNumber(str):
    def __float__(self):
        return 3.25


class BytesNumber(bytes):
    def __float__(self):
        return 4.25


class BadIndex:
    def __index__(self):
        return 3.5


class Broken:
    def __float__(self):
        raise ValueError('conversion failed')


class FloatResult:
    def __float__(self):
        return FloatOverride(2.5)


class Descriptor:
    def __get__(self, obj, owner):
        gc.collect()
        owner.__float__ = lambda self: 9.0
        return lambda: 6.5


class Described:
    __float__ = Descriptor()


class IndexDescriptor:
    def __get__(self, obj, owner):
        gc.collect()
        owner.__index__ = lambda self: 10
        return lambda: 8


class DescribedIndex:
    __index__ = IndexDescriptor()


def show_error(fn, value):
    try:
        fn(value)
    except Exception as exc:
        print(type(exc).__name__, str(exc))


def pack(value):
    return struct.pack('>d', value)


print(math.fabs(FloatOverride(-1.5)))
print(struct.unpack('>d', pack(FloatOverride(-1.5)))[0])
print(math.fabs(IntOverride(2)))
print(math.fabs(IntIndex(-3)))
print(int.__float__(IntIndex(-3)), float(IntIndex(-3)), int.__float__(True))
print(int.__float__(IntOverride(2)), float(IntOverride(2)))
print(math.fsum([IndexOnly(), IntOverride(1), FloatOverride(2.5)]))
print(struct.unpack('>d', pack(IndexOnly()))[0])
print(math.fabs(StringNumber('not a literal')))
print(struct.unpack('>d', pack(BytesNumber(b'not a literal')))[0])
with warnings.catch_warnings(record=True) as caught:
    warnings.simplefilter('always', DeprecationWarning)
    line = sys._getframe().f_lineno + 1
    print('subclass', math.fabs(FloatResult()))
    warning = caught[0]
    print('warning', warning.category.__name__, str(warning.message))
    print('location', warning.filename == __file__, warning.lineno == line, len(caught))

with warnings.catch_warnings(record=True) as caught:
    warnings.simplefilter('always', DeprecationWarning)
    line = sys._getframe().f_lineno + 1
    print('index subclass', math.fabs(IndexSubclass()))
    warning = caught[0]
    print('index warning', warning.category.__name__, str(warning.message))
    print('index location', warning.filename == __file__, warning.lineno == line, len(caught))

with warnings.catch_warnings(record=True) as caught:
    warnings.simplefilter('error', DeprecationWarning)
    show_error(math.fabs, FloatResult())
    show_error(pack, FloatResult())
    show_error(math.fabs, IndexSubclass())
    show_error(pack, IndexSubclass())
    print('error records', len(caught))


def warning_callback(message, category, filename, lineno, file=None, line=None):
    gc.collect()
    print('callback', category.__name__, filename == __file__)


def failing_warning(message, category, filename, lineno, file=None, line=None):
    gc.collect()
    raise RuntimeError('warning callback failed')


with warnings.catch_warnings():
    warnings.simplefilter('always', DeprecationWarning)
    warnings.showwarning = warning_callback
    print('callback result', math.fabs(FloatResult()))
    warnings.showwarning = failing_warning
    show_error(math.fabs, FloatResult())
    show_error(pack, FloatResult())

print(math.fabs(Described()), math.fabs(Described()))
print(math.fabs(DescribedIndex()), math.fabs(DescribedIndex()))
show_error(math.fabs, BadFloat())
show_error(math.fabs, BadIndex())
show_error(math.fabs, Broken())
show_error(math.fabs, '1.5')
show_error(pack, BadFloat())
show_error(pack, Broken())
show_error(pack, '1.5')
