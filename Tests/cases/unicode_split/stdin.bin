for text in ('', ' ', ' a  b c  ', 'a,,b,', 'α::β::', '\u2003one\u001ctwo\u3000'):
    for sep in (None, ',', '::'):
        for count in (-1, 0, 1, 2):
            print(repr(text), repr(sep), count, text.split(sep, count), text.rsplit(sep, count))
class Index:
    def __index__(self):
        return 1
print('a.b.c'.split('.', Index()))
class Text(str):
    pass
print(type(Text('a').split()[0]) is str)
for sep, count in (('', 0), (None, None), (3, 1), (None, 2**100)):
    try:
        'a'.split(sep, count)
    except (ValueError, TypeError, OverflowError) as e:
        print(type(e).__name__)
