import gc

for value in ('bad', '', '0xq', '\x00', '\uff11x', 'x' * 230,
              b'bad', bytearray(b'bad'), memoryview(b'bad'), b'\xff', b'\x00', b'x' * 230):
    for base in (None, 0, 2, 16):
        if isinstance(value, memoryview) and base is not None:
            continue
        try:
            int(value) if base is None else int(value, base)
        except ValueError as error:
            print(str(error))

class Text(str):
    def __repr__(self):
        gc.collect()
        return '<custom text>'

class Raw(bytes):
    def __repr__(self):
        raise AssertionError('buffer diagnostics use an exact bytes snapshot')

for value in (Text('bad'), Raw(b'bad')):
    try:
        int(value)
    except ValueError as error:
        print('subtype', str(error))

class BadText(str):
    def __repr__(self):
        gc.collect()
        raise LookupError('repr failed')

try:
    int(BadText('bad'))
except LookupError as error:
    print(type(error).__name__, str(error))
