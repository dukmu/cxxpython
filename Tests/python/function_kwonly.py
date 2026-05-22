def pair(first, *, second, third=3):
    print(first)
    print(second)
    print(third)


pair(1, second=2)
pair(1, second=2, third=4)
print(pair.__kwdefaults__)
print(pair.__text_signature__)
pair.__kwdefaults__ = {'third': 9}
pair(1, second=2)
pair.__kwdefaults__ = None

try:
    pair(1, second=2)
except TypeError as exc:
    print(str(exc).__contains__('missing required keyword-only argument'))

try:
    pair(1, 2, second=3)
except TypeError as exc:
    print(str(exc).__contains__('2 arguments'))


def collector(*items, flag=False, **kwargs):
    print(items)
    print(flag)
    print(kwargs)


collector(1, 2, flag=True, name='Ada')


def outer():
    seed = 40

    def inner(*, value=seed + 2):
        return value

    return inner()


print(outer())
print(outer.__code__.co_kwonlyargcount)
