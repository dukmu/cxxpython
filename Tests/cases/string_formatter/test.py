import _string
import string


def attempt(function, *args):
    try:
        print(function(*args))
    except Exception as exc:
        print(type(exc).__name__, repr(str(exc)))


formatter = string.Formatter()
print(list(formatter.parse('a{{b}} {name!r:>{width}} end')))
print(formatter.get_field('0[x][1]', ({'x': [10, 20]},), {}))
print(formatter.vformat('{0!r:>{width}} / {name!s}', ('hi',),
                        {'width': 6, 'name': 42}))
attempt(formatter.format, '{} {0}', 1)
attempt(formatter.format, '{0!z}', 1)
attempt(formatter.format, '{0:{1:{2}}}', 1, 2, 3)

class Text(str):
    pass


print(list(_string.formatter_parser(Text('a{}b'))))
first, rest = _string.formatter_field_name_split(Text('١.foo[٢][key]'))
print(first, list(rest))
it = _string.formatter_parser('good{} trailing {')
print(next(it))
attempt(next, it)
attempt(next, it)
first, it = _string.formatter_field_name_split('x.ok[]')
print(first, next(it))
attempt(next, it)
attempt(next, it)
