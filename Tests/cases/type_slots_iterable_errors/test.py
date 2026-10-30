for slots in ([1], [None], [['obj']], ['bad name'], [''], ['obj', 1]):
    try:
        type('BadSlots', (), {'__slots__': slots})
    except TypeError as exc:
        print(type(exc).__name__, str(exc))

for slots in (None, 123):
    try:
        type('NotIterable', (), {'__slots__': slots})
    except TypeError:
        print('not iterable')

failure = ValueError('iterator failure')
events = []
def failing_names():
    events.append('first')
    yield 123
    events.append('later')
    raise failure
try:
    type('Failing', (), {'__slots__': failing_names()})
except ValueError as exc:
    print(exc is failure, str(exc), events)

class BadIter:
    def __iter__(self):
        raise failure
try:
    type('FailingIter', (), {'__slots__': BadIter()})
except ValueError as exc:
    print(exc is failure)

class BadHint:
    def __iter__(self):
        return iter(['obj'])
    def __length_hint__(self):
        raise failure
try:
    type('FailingHint', (), {'__slots__': BadHint()})
except ValueError as exc:
    print(exc is failure)

events = []
def invalid_names():
    events.append('first')
    yield 1
    events.append('last')
    yield 'obj'
try:
    type('Consumed', (), {'__slots__': invalid_names()})
except TypeError as exc:
    print(str(exc), events)
