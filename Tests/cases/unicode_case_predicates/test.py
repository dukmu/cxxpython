# CPython 3.12.14 Objects/unicodeobject.c unicode_{isupper,islower,istitle}_impl
# and Unicode 15.0 property cases; PSF license: doc/licenses/python-3.12.14.txt.


def predicates(text):
    return text.isupper(), text.islower(), text.istitle()


for text in (
    '', '123 _-\t\n', '\x00\ud800\U0010ffff', '\u4e2d\U0001f40d',
    'A', 'a', 'ABC', 'abc', 'Abc', 'aBc', 'ABC 123', 'abc 123',
    'Hello World', 'Hello world', 'Hello_World', 'Hello-World', 'They\'Re',
    '\xc4\xd6', '\xe4\xf6', '\xc4pfel \xd6l', '\xdf', '\u1e9e',
    '\u03a3', '\u03c2\u03c3', '\u03a3\u03c2', '\u0410\u0431',
    '\u01c4', '\u01c5', '\u01c6', '\u01c5uro', '\u01c5\u01c5',
    '\u01c5 \u01c5', '\u01c5\u01c6', '\u01c6\u01c5', '\u1f88',
    '\u2160', '\u2170', '\u24b6', '\u24d0', '\u02b0', '\u0345',
    '\u0301', 'A\u0301', 'A\u0301b', 'A\u0301B', 'a\u0301',
    '\u4e2dA\u4e2d', '\u4e2da\u4e2d', 'A\x00B', 'A\x00b',
    '\U00010400', '\U00010428', '\U00010400\U00010428',
    '\U0001d400', '\U0001d41a', '\U0001d400\U0001d41a',
):
    print(ascii(text), predicates(text))


class Text(str):
    def __str__(self):
        raise AssertionError('case predicates must use the native string payload')

    def __len__(self):
        raise AssertionError('case predicates must use the native string length')

    def __iter__(self):
        raise AssertionError('case predicates must not call Python iteration')


for text in ('', '123', 'ABC', 'abc', '\u01c5uro', 'A\u0301b'):
    value = Text(text)
    result = predicates(value)
    print('subclass', ascii(text), result, all(type(item) is bool for item in result))
    print('unbound', str.isupper(value), str.islower(value), str.istitle(value))

for method in (str.isupper, str.islower, str.istitle):
    for value in (None, 1, b'A', object()):
        try:
            method(value)
        except TypeError as exc:
            print('receiver', str(exc))
        else:
            raise AssertionError('non-string receiver accepted')

# Cross several poll intervals before success, all-uncased failure, or rejection.
for label, text in (
    ('upper', '\u4e2d\u03a3' * 10000),
    ('lower', '\u4e2d\u03c3' * 10000),
    ('title', '\u01c5uro ' * 10000),
    ('uncased', '\u4e2d\U0001f40d' * 10000),
    ('upper late lower', '\u03a3' * 20000 + '\u03c3'),
    ('lower late upper', '\u03c3' * 20000 + '\u03a3'),
    ('title late upper', '\u01c5uro ' * 10000 + 'AB'),
    ('uncased late upper', '\u4e2d' * 20000 + '\u03a3'),
    ('uncased late lower', '\u4e2d' * 20000 + '\u03c3'),
):
    print('long', label, predicates(Text(text)))
