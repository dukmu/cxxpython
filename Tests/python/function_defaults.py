def greet(name, prefix='hello', suffix='!'):
    return prefix + ', ' + name + suffix


print(greet('Ada'))
print(greet('Ada', 'hi'))
print(greet('Ada', suffix='.'))
print(greet.__defaults__)
print(greet.__text_signature__)
greet.__defaults__ = ('yo', '?')
print(greet('Ada'))
greet.__defaults__ = None


def capture(value=[]):
    value.append(1)
    return value


print(capture())
print(capture())


def with_varargs(first, second=2, *rest, **kwargs):
    print(first)
    print(second)
    print(rest)
    print(kwargs)


with_varargs(1)
with_varargs(1, 3, 4, flag=True)

try:
    greet()
except TypeError as exc:
    print(str(exc).__contains__('missing required positional argument'))

try:
    greet('Ada', 'hi', '!', '?')
except TypeError as exc:
    print(str(exc).__contains__('takes 3 positional arguments'))


def outer():
    seed = 40

    def inner(value=seed + 2):
        return value

    return inner()


print(outer())
