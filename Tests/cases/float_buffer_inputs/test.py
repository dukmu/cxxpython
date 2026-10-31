import gc
import pickle

for source in (b' 1_2.5 ', bytearray(b' 1_2.5 '), memoryview(b' 1_2.5 '),
               pickle.PickleBuffer(b' 1_2.5 ')):
    assert float(source) == 12.5
    print('buffer', type(source).__name__, float(source))


class Number(bytes):
    def __float__(self):
        gc.collect()
        return 4.5


class Index(bytearray):
    def __index__(self):
        gc.collect()
        return 7


print('precedence', float(Number(b'wrong')), float(Index(b'wrong')))
for source in (b'invalid', b'1\x00', bytearray(b''), memoryview(b'abc')):
    try:
        float(source)
    except ValueError:
        print('invalid literal', type(source).__name__)
    else:
        raise AssertionError(source)
for source in (object(), memoryview(b'1234')[::2]):
    try:
        float(source)
    except TypeError:
        print('unsupported', type(source).__name__)
    else:
        raise AssertionError(source)

view = memoryview(b'1')
view.release()
try:
    float(view)
except TypeError:
    print('released view rejected')
else:
    raise AssertionError('released buffer converted')
