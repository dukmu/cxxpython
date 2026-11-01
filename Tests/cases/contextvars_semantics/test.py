from contextvars import Context, ContextVar, Token

var = ContextVar('color', default='blue')
same_name = ContextVar('color', default='blue')
one = Context()
two = Context()
print(one == two, one != two, one == {})
one.run(var.set, [1, 2])
two.run(var.set, [1, 2])
print(one == two, one != two)
two.run(same_name.set, [1, 2])
print(one == two, one != two)
two = one.copy()
print(one == two)


class FailingEquality:
    def __eq__(self, other):
        raise ValueError('comparison failed')


one.run(var.set, FailingEquality())
two.run(var.set, FailingEquality())
try:
    one == two
except ValueError:
    print('comparison error')
try:
    hash(one)
except TypeError:
    print('unhashable')

text = repr(var)
print(text.startswith("<ContextVar name='color' default='blue' at 0x"), text.endswith('>'))
plain = ContextVar('plain')
print('default=' not in repr(plain), "name='plain'" in repr(plain))
token = plain.set(42)
print(repr(token).startswith('<Token var=<ContextVar name=\'plain\''))
print(repr(Token.MISSING))
plain.reset(token)
print(repr(token).startswith('<Token used var=<ContextVar name=\'plain\''))
