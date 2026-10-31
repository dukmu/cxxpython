def rejected(label, function, *args):
    try:
        function(*args)
    except Exception as exc:
        print(label, type(exc).__name__, str(exc))
    else:
        print(label, 'missing exception')


for value in [1, 'str', [65], memoryview(b'abcd')[::2]]:
    rejected('bytes', bytes.__add__, b'x', value)
    rejected('bytearray', bytearray.__add__, bytearray(b'x'), value)
    mutable = bytearray(b'x')
    rejected('inplace', bytearray.__iadd__, mutable, value)
    print('unchanged', mutable)

view = memoryview(b'ab')
view.release()
rejected('released bytes', bytes.__add__, b'x', view)
rejected('released bytearray', bytearray.__add__, bytearray(b'x'), view)
rejected('released inplace', bytearray.__iadd__, bytearray(b'x'), view)


class Conversion:
    def __bytes__(self):
        raise AssertionError('must not convert via __bytes__')

    def __iter__(self):
        raise AssertionError('must not convert via iteration')


rejected('no bytes conversion', bytes.__add__, b'x', Conversion())
rejected('no iterable conversion', bytearray.__iadd__, bytearray(b'x'), Conversion())
