events = []


def subject():
    events.append('subject')
    return [1, 2, 3]


match subject():
    case [0, *_]:
        print('wrong')
    case [first, *middle, last] as whole if (total := first + last) == 4:
        print('sequence', first, middle, last, whole, total, events)


class Values:
    key = 'key'
    number = 42


for value in (None, True, False, 42, 'other'):
    match value:
        case None:
            print('singleton', 'none')
        case True:
            print('singleton', 'true')
        case False:
            print('singleton', 'false')
        case Values.number:
            print('value', value)
        case _:
            print('wildcard', value)

match {'key': 1, 'extra': 2}:
    case {Values.key: captured, **rest}:
        print('mapping', captured, rest)

for value in ([1, 2], [2, 1]):
    match value:
        case [1, x] | [x, 1]:
            print('or', x)

match [2, 10, 20]:
    case [1, left, right] | [2, right, left]:
        print('or reorder', left, right)

match (10, 20):
    case (x, y) if False:
        pass
    case _:
        print('guard bindings', x, y)

global_name = 'old'


def scopes():
    global global_name
    local = 'old'

    def inner():
        nonlocal local
        global global_name
        match ('global', 'nonlocal'):
            case (global_name, local):
                return lambda: (global_name, local)

    closure = inner()
    print('scope', closure(), local, global_name)


scopes()


class Private:
    match (7,):
        case (__captured,):
            pass

    def get(self):
        return self.__captured


print('private', Private().get())


class Names:
    match (1, 2):
        case (__x, _Names__x):
            pass


class Reorder:
    match (0, 10, 20):
        case (1, __x, _Reorder__x) | (0, _Reorder__x, __x):
            pass


print('private aliases', Names._Names__x, Reorder._Reorder__x)

match {9007199254740993: 'integer', 9007199254740993.0: 'float'}:
    case {9007199254740993: integer, 9007199254740993.0: floating}:
        print('exact keys', integer, floating)

match {'x': 'text', b'x': 'bytes'}:
    case {'x': text, b'x': binary}:
        print('distinct keys', text, binary)


def generator():
    match 3:
        case n if (yield n):
            yield ('matched', n)
        case _:
            yield 'missed'


g = generator()
print('yield guard', next(g), g.send(True))


class Awaitable:
    def __await__(self):
        yield 'pause'
        return True


async def coroutine():
    match 4:
        case n if await Awaitable():
            return n


c = coroutine()
print('await guard', c.send(None))
try:
    c.send(None)
except StopIteration as exc:
    print('await result', exc.value)
