import numbers


for value in (True, 7, 2.5, 1 + 2j, '7'):
    print(type(value).__name__,
          isinstance(value, numbers.Number),
          isinstance(value, numbers.Complex),
          isinstance(value, numbers.Real),
          isinstance(value, numbers.Rational),
          isinstance(value, numbers.Integral))

# dill.source.indent uses precisely this Integral classification.
def indent_width(spaces):
    if isinstance(spaces, numbers.Integral):
        spaces = ' ' * spaces
    return spaces


print(repr(indent_width(2)), repr(indent_width('\t')))
print(issubclass(numbers.Integral, numbers.Rational))


class Registered:
    pass


print(isinstance(Registered(), numbers.Integral))
print(numbers.Integral.register(Registered) is Registered)
print(isinstance(Registered(), numbers.Integral))
print(issubclass(Registered, numbers.Number))
try:
    numbers.Integral()
except TypeError:
    print('abstract Integral')
