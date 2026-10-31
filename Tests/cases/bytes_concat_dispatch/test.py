import gc

events = []


class Reflected:
    def __radd__(self, left):
        gc.collect()
        events.append(type(left).__name__)
        return 'reflected'


for left in [b'x', bytearray(b'x')]:
    print('reflected', left + Reflected())
    left += Reflected()
    print('inplace fallback', left)
print('calls', events)


class Buffer(bytearray):
    def __radd__(self, left):
        gc.collect()
        self.append(33)
        return NotImplemented


print('buffer after reflected callback', b'x' + Buffer(b'y'))
print('mutable after reflected callback', bytearray(b'x') + Buffer(b'y'))


class Bytes(bytes):
    def __radd__(self, left):
        gc.collect()
        return 'bytes subclass reflected'


class ByteArray(bytearray):
    def __radd__(self, left):
        gc.collect()
        return 'bytearray subclass reflected'


print('subtype priority', b'x' + Bytes(b'y'), bytearray(b'x') + ByteArray(b'y'))
print('direct bypasses reflection', bytes.__add__(b'x', Bytes(b'y')),
      bytearray.__add__(bytearray(b'x'), ByteArray(b'y')))


class Broken:
    def __radd__(self, left):
        gc.collect()
        raise RuntimeError('reflected failure')


for left in [b'x', bytearray(b'x')]:
    try:
        left + Broken()
    except RuntimeError as exc:
        print(type(exc).__name__, str(exc))
    else:
        print('missing exception')
