import gc

for text, part in [('abab', 'ab'), ('abba', 'ab'), ('', ''), ('abc', ''),
                   ('abc', 'abcd'), ('\u03b1\x00\u03b1', '\u03b1'), ('\ud800x', '\ud800')]:
    print(repr(text.removeprefix(part)), repr(text.removesuffix(part)))

text = 'unchanged value'
print('identity', text.removeprefix('missing') is text, text.removesuffix('') is text)


class String(str):
    def __str__(self):
        raise AssertionError('must use native contents')

    def __getitem__(self, key):
        raise AssertionError('must use native contents')


for method in (str.removeprefix, str.removesuffix):
    for part in (String('ab'), String(''), String('missing')):
        result = method(String('abab'), part)
        gc.collect()
        print('subclass', type(result) is str, repr(result))
    for args in [(1, 'ab'), ('ab', 1), ('ab', ('a', 'b'))]:
        try:
            method(*args)
        except TypeError as error:
            print(type(error).__name__, str(error))
        else:
            raise AssertionError(args)
