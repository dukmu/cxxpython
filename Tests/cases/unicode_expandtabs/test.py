# Adapted from CPython 3.12.14 Lib/test/string_tests.py test_expandtabs and
# Objects/unicodeobject.c unicode_expandtabs_impl; PSF license:
# doc/licenses/python-3.12.14.txt.
import gc


def show(label, result):
    print(label, repr(result))


def error(label, function, *args, **kwargs):
    try:
        function(*args, **kwargs)
    except Exception as exc:
        # Upstream checks TypeError by class, not the shared binder/index wording.
        if isinstance(exc, TypeError):
            show(label, type(exc).__name__)
        else:
            show(label, (type(exc).__name__, str(exc)))
    else:
        raise AssertionError(label)


source = 'abc\rab\tdef\ng\thi'
show('default', source.expandtabs())
show('eight', source.expandtabs(8))
show('four', source.expandtabs(4))
show('keyword', source.expandtabs(tabsize=4))
show('CRLF', 'abc\r\nab\tdef\ng\thi'.expandtabs(4))
show('unchanged CRLF', 'abc\r\nab\r\ndef\ng\r\nhi'.expandtabs(4))
show('one-column tabs', ' \ta\n\tb'.expandtabs(1))
for size in (0, -1, -8, 1, 2, 3, 8):
    show('sizes ' + str(size), '\tA\tB\t\r\tC\n\tD\r\n\t'.expandtabs(size))
show('tab at stop', '12345678\t9'.expandtabs())
show('other whitespace', 'a\vb\fc\u0085d\u2028e\u2029f\tg'.expandtabs(8))

# Columns count Unicode code points, not UTF-8 bytes or terminal display cells.
for source in ('\xe4\tZ', '\u4e2d\u6587\tZ', '\U0001f40d\tZ',
               'e\u0301\tZ', '\x00\tZ', '\ud800\tZ'):
    show('unicode', source.expandtabs(4))
show('unicode reset', '\u4e2d\tX\r\xe4\tY\n\U0001f40d\tZ'.expandtabs(4))

for source in ('', 'unchanged', '\xe4\u4e2d\U0001f40d'):
    result = source.expandtabs()
    show('exact unchanged', (result, type(result) is str, result is source))
show('C int maximum', 'no tabs'.expandtabs((1 << 31) - 1))
show('C int minimum', 'a\tb'.expandtabs(-(1 << 31)))
show('bool true', 'a\tb'.expandtabs(True))
show('bool false', 'a\tb'.expandtabs(False))


class Text(str):
    def __str__(self):
        raise AssertionError('expandtabs must use the native string payload')

    def __len__(self):
        raise AssertionError('expandtabs must use the native string length')


for source in ('', 'unchanged', 'a\tb', '\u4e2d\t\xe4'):
    value = Text(source)
    result = value.expandtabs(4)
    show('subtype', (result, type(result) is str, result is value))


class Size:
    def __init__(self, value):
        self.value = value
        self.calls = 0

    def __index__(self):
        self.calls += 1
        gc.collect()
        return self.value


size = Size(4)
show('index callback', Text('a\tb').expandtabs(size))
show('index calls', size.calls)
size = Size(2)
show('empty callback', Text('').expandtabs(tabsize=size))
show('empty calls', size.calls)


class IntSize(int):
    def __index__(self):
        raise AssertionError('int payload must bypass overridden __index__')


show('int subtype', 'a\tb'.expandtabs(IntSize(4)))
for size in (1 << 31, -(1 << 31) - 1, 1 << 100, -(1 << 100)):
    error('overflow', 'no tabs'.expandtabs, size)
error('overflow callback', 'a\tb'.expandtabs, Size(1 << 100))
for size in (None, 2.0, '4', object()):
    error('bad size', 'a\tb'.expandtabs, size)


class BadIndex:
    def __index__(self):
        gc.collect()
        return 'four'


class BrokenIndex:
    def __index__(self):
        gc.collect()
        raise ValueError('tabsize callback failed')


error('bad index result', 'a\tb'.expandtabs, BadIndex())
error('index failure', Text('a\tb').expandtabs, BrokenIndex())
error('bad receiver', str.expandtabs, 1)
error('too many arguments', 'a\tb'.expandtabs, 4, 8)
error('unknown keyword', 'a\tb'.expandtabs, unknown=4)

# Exercise both scanning passes and a single tab's chunked space fill.
source = ('\u4e2d\tX\r\n' * 4097) + 'e\u0301\tZ'
result = source.expandtabs(4)
show('long scan', (len(result), result == '\u4e2d   X\r\n' * 4097 + 'e\u0301  Z'))
result = '\tX'.expandtabs(16385)
show('large fill', (len(result), result == ' ' * 16385 + 'X'))
source = 'x' * 16385
show('long unchanged', source.expandtabs() is source)
