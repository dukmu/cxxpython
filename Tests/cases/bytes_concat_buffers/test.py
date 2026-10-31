import gc
import pickle


class Bytes(bytes):
    def __bytes__(self):
        raise AssertionError('concatenation must read the buffer')


class ByteArray(bytearray):
    def __bytes__(self):
        raise AssertionError('concatenation must read the buffer')


operands = [b'\x00z', bytearray(b'\x00z'), Bytes(b'\x00z'), ByteArray(b'\x00z'),
            memoryview(b'\x00z'), memoryview(bytearray(b'\x00z')),
            pickle.PickleBuffer(b'\x00z'), pickle.PickleBuffer(bytearray(b'\x00z'))]
for left in [b'x', bytearray(b'x'), Bytes(b'x'), ByteArray(b'x')]:
    results = [left + right for right in operands]
    print(type(left).__name__, [type(result).__name__ for result in results],
          all(bytes(result) == b'x\x00z' for result in results))

typed = memoryview(b'abcd').cast('I')
print('typed', b'x' + typed, bytearray(b'x') + typed)
print('direct', bytes.__add__(b'x', bytearray(b'y')),
      bytearray.__add__(bytearray(b'x'), memoryview(b'y')))

right = b'right'
left = b'left'
print('empty identities', b'' + right is right, left + b'' is left,
      Bytes(b'') + right is right, left + memoryview(b'') is left)
print('subclass result', type(Bytes(b'left') + b'').__name__,
      type(Bytes(b'left') + Bytes(b'right')).__name__,
      type(ByteArray(b'left') + b'').__name__)

value = bytearray(b'a\x00\xff')
encoded = pickle.dumps(value, protocol=5)
decoded = pickle.loads(encoded)
print('pickle protocol 5', type(encoded).__name__, type(decoded).__name__, decoded == value)

for constructor in [bytes, bytearray, Bytes, ByteArray]:
    sources = [b'\x00z', bytearray(b'\x00z'), memoryview(b'\x00z'),
               pickle.PickleBuffer(b'\x00z'), pickle.PickleBuffer(bytearray(b'\x00z'))]
    results = [constructor(source) for source in sources]
    print('buffer constructors', constructor.__name__,
          [type(result).__name__ for result in results],
          all(result == b'\x00z' for result in results))
    print('strided constructor', constructor.__name__, constructor(memoryview(b'abcd')[::2]))

events = []


class Counted(bytearray):
    def __index__(self):
        gc.collect()
        events.append('index')
        return 3

    def __iter__(self):
        raise AssertionError('buffer must precede iteration')


class CountFailed(Counted):
    def __index__(self):
        gc.collect()
        events.append('index TypeError')
        raise TypeError('not a count')


class BytesHook(Counted):
    def __bytes__(self):
        gc.collect()
        events.append('bytes')
        return b'hook'


for constructor in [bytes, bytearray]:
    events.clear()
    print('index before buffer', constructor.__name__, constructor(Counted(b'xy')), events)
    events.clear()
    print('index failure to buffer', constructor.__name__, constructor(CountFailed(b'xy')), events)
    events.clear()
    print('bytes hook precedence', constructor.__name__, constructor(BytesHook(b'xy')), events)


class Iterable:
    def __index__(self):
        gc.collect()
        raise TypeError('not a count')

    def __iter__(self):
        gc.collect()
        return iter([65, 66])


print('unsupported buffer to iterable', bytes(Iterable()), bytearray(Iterable()))


class IndexErrorBuffer(Counted):
    def __index__(self):
        gc.collect()
        raise RuntimeError('count failure')


class Text(str):
    def __index__(self):
        raise AssertionError('string must precede index')


for constructor in [bytes, bytearray]:
    for source in [IndexErrorBuffer(b'xy'), Text('xy')]:
        try:
            constructor(source)
        except Exception as exc:
            print('constructor rejection', constructor.__name__, type(exc).__name__)
        else:
            print('missing constructor rejection')


class TextHook(Text):
    def __bytes__(self):
        return b'text hook'


print('text bytes hook', bytes(TextHook('xy')))
print('encoding before bytes hook', bytes(TextHook('xy'), 'ascii'),
      bytearray(TextHook('xy'), 'ascii'))

view = memoryview(b'xy')
view.release()
exported = pickle.PickleBuffer(b'xy')
exported.release()
for constructor in [bytes, bytearray, Bytes, ByteArray]:
    for source in [view, exported]:
        try:
            constructor(source)
        except Exception as exc:
            print('released constructor', constructor.__name__, type(exc).__name__, str(exc))
        else:
            print('missing released constructor exception')
