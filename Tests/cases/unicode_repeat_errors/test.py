# CPython 3.12 sequence-repeat index conversion, overflow, and errors.
for count in [1.5, '2', None, object()]:
    for operation in [lambda: 'ab' * count,
                      lambda: count * 'ab',
                      lambda: str.__mul__('ab', count),
                      lambda: str.__rmul__('ab', count)]:
        try:
            operation()
        except TypeError as error:
            print(type(error).__name__)

class BadIndex:
    def __index__(self):
        return 2.5

try:
    'a' * BadIndex()
except TypeError as error:
    print('bad index', type(error).__name__)

class IndexFailure:
    def __index__(self):
        raise ValueError('index callback failed')

try:
    IndexFailure() * 'a'
except ValueError as error:
    print(type(error).__name__, str(error))
for count in [10**100, -10**100]:
    for text in ['', 'ab']:
        try:
            text * count
        except OverflowError as error:
            print(type(error).__name__, str(error))
for operation in [lambda: 'ab' * (2**62), lambda: (2**62) * 'ab']:
    try:
        operation()
    except OverflowError as error:
        print(type(error).__name__, str(error))
print(repr('' * (2**63 - 1)), repr('ab' * (-2**63)))
try:
    str.__mul__(42, 2)
except TypeError as error:
    print('receiver', type(error).__name__)
