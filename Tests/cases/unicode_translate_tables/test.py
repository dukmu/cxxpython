# CPython 3.12 test_unicode.py maketrans and translate semantics.
print(str.maketrans('abc', 'ABC', 'bc'))
print('abracadabra'.translate(str.maketrans('abc', 'ABC', 'bc')))
print('abc\u03b1'.translate(str.maketrans({'a': 'xy', 'b': None, 99: 0x1f600})))
print('abc'.translate([None] * 97 + [65, None, 'XY']))
print('abc'.maketrans('a', 'z'))
print(str.maketrans({True: 2000000, 'a': []}))
for args in [(), (1,), ({}, None), (1, 'a'), ('a', 1), ('a', 'b', None),
             ('a', 'bb'), ('a', 'b', 'c', 'd'), ({'ab': 1},), ({1.5: 2},)]:
    try:
        str.maketrans(*args)
    except (TypeError, ValueError) as error:
        print(type(error).__name__, str(error))
class D(dict):
    pass
try:
    str.maketrans(D())
except TypeError as error:
    print(type(error).__name__, str(error))
for value in [-1, 0x110000, 10**100, 1.5, [], object()]:
    try:
        'a'.translate({97: value})
    except (TypeError, ValueError) as error:
        print(type(error).__name__, str(error))
