import io
import token
import tokenize
import _tokenize


sources = [
    ('single string', "x = 'unfinished\n"),
    ('triple string', 'x = """unfinished\nsecond\n'),
    ('f-string', 'f"{value\n'),
    ('indent', 'if x:\n    a\n  b\n'),
    ('open bracket', 'x = (1\n'),
    ('wrong bracket', 'x = (1]\n'),
    ('numbers', '0x 0b102 1e+ 0123 1__2 42xyz\n'),
    ('continuation', 'x = 1 \\\n'),
]


def error(exc):
    attrs = ('msg', 'filename', 'lineno', 'offset', 'end_lineno',
             'end_offset', 'text')
    return (type(exc).__name__, exc.args,
            tuple((name, getattr(exc, name, None)) for name in attrs))


for label, source in sources:
    print('source', repr((label, source)))
    for mode in ('public', 'native-extra', 'native-plain'):
        reader = io.StringIO(source).readline
        if mode == 'public':
            iterator = tokenize.generate_tokens(reader)
        else:
            iterator = _tokenize.TokenizerIter(reader, extra_tokens=mode == 'native-extra')
        items = []
        try:
            for item in iterator:
                items.append((token.tok_name[item[0]],) + tuple(item[1:]))
            result = ('ok',)
        except Exception as exc:
            result = error(exc)
        print(mode, repr((items, result)))
