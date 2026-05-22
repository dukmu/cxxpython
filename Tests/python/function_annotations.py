"module docs"

Marker = 'marker'


def annotated(first: int, second: Marker = 'x', /, third: str = 'y', *items: int, flag: bool = True, **options: str) -> Marker:
    "annotation docs"
    return second


print(annotated.__doc__)
print(annotated.__annotations__)
print(annotated.__annotations__['first'] is int)
print(annotated.__annotations__['second'])
print(annotated.__annotations__['third'] is str)
print(annotated.__annotations__['items'] is int)
print(annotated.__annotations__['flag'] is bool)
print(annotated.__annotations__['options'] is str)
print(annotated.__annotations__['return'])
annotated.__annotations__ = {'custom': 1}
print(annotated.__annotations__)
annotated.__annotations__ = None
print(annotated.__annotations__)
annotated.__doc__ = 'new docs'
print(annotated.__doc__)


def outer():
    local = 'local annotation'

    def inner(value: local) -> local:
        return value

    print(inner.__annotations__['value'])
    print(inner.__annotations__['return'])


outer()


class WithDoc:
    "class docs"


def no_doc():
    return None


print(__doc__)
print(WithDoc.__doc__)
print(no_doc.__doc__)
