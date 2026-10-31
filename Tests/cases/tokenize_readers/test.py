import io
import _tokenize
import tokenize


def outcome(action):
    try:
        value = action()
        return ('return', value)
    except Exception as exc:
        return ('error', type(exc).__name__, exc.args)


class Reader:
    def __init__(self, values):
        self.values = list(values)
        self.calls = 0

    def __call__(self):
        self.calls += 1
        if not self.values:
            return ''
        value = self.values.pop(0)
        if isinstance(value, Exception):
            raise value
        return value


for extra in (False, True):
    reader = Reader(['x = 1\n', 'y\n', ''])
    iterator = _tokenize.TokenizerIter(reader, extra_tokens=extra)
    print('constructed', repr((extra, reader.calls, iter(iterator) is iterator)))
    for i in range(12):
        result = outcome(lambda: next(iterator))
        print('next', repr((i, reader.calls, result)))
        if result[:2] == ('error', 'StopIteration'):
            print('again', repr((outcome(lambda: next(iterator)), reader.calls)))
            break

for label, values in [
    ('throws initially', [ValueError('reader failed'), 'x\n', '']),
    ('throws later', ['x\n', ValueError('reader failed'), 'y\n', '']),
    ('bytes', [b'x\n', b'']),
    ('non-string', [123]),
]:
    reader = Reader(values)
    iterator = _tokenize.TokenizerIter(reader, extra_tokens=True)
    print('reader', repr((label, reader.calls)))
    for i in range(7):
        result = outcome(lambda: next(iterator))
        print('step', repr((i, reader.calls, result)))
        if result[0] == 'error':
            break

for label, action in [
    ('missing', lambda: _tokenize.TokenizerIter()),
    ('missing extra', lambda: _tokenize.TokenizerIter(lambda: '')),
    ('positional extra', lambda: _tokenize.TokenizerIter(lambda: '', True)),
    ('unknown keyword', lambda: _tokenize.TokenizerIter(lambda: '', extra_tokens=True, unknown=1)),
    ('noncallable', lambda: next(_tokenize.TokenizerIter(123, extra_tokens=True))),
    ('non-bool extra', lambda: list(_tokenize.TokenizerIter(lambda: '', extra_tokens='yes'))),
    ('public str reader', lambda: list(tokenize.tokenize(io.StringIO('x\n').readline))),
    ('public bytes reader', lambda: list(tokenize.generate_tokens(io.BytesIO(b'x\n').readline))),
    ('bad encoding', lambda: list(tokenize.tokenize(io.BytesIO(b'# coding: no-such-codec\nx\n').readline))),
    ('bom conflict', lambda: list(tokenize.tokenize(io.BytesIO(b'\xef\xbb\xbf# coding: latin-1\n').readline))),
]:
    print('validation', repr((label, outcome(action))))
