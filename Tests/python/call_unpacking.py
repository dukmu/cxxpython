def show(*args, **kwargs):
    print(args)
    print(kwargs)


show(1, *(2, 3), tail=4)
show(*[1, 2], **{'name': 'value'})
show(a=1, **{'b': 2})

try:
    show(a=1, **{'a': 2})
except TypeError as exc:
    print(str(exc).__contains__('multiple values for keyword argument'))

try:
    show(**{1: 'bad'})
except TypeError as exc:
    print(str(exc).__contains__('keywords must be strings'))

try:
    show(*1)
except TypeError as exc:
    print(str(exc).__contains__('not iterable'))
