import gc
import pickle


class Mutable(bytearray):
    pass


for source in [b'y', bytearray(b'y'), memoryview(b'y'), pickle.PickleBuffer(b'y')]:
    value = Mutable(b'x')
    original = value
    value += source
    gc.collect()
    print('inplace', type(value).__name__, value is original, value)

value = bytearray(b'x')
result = value + b'y'
print('ordinary is separate', type(result).__name__, result is value, value, result)
immutable = b'x'
original = immutable
immutable += bytearray(b'y')
print('immutable augmented', type(immutable).__name__, immutable, original)

for value in [bytearray(b'ab'), Mutable(b'ab')]:
    try:
        value += value
    except BufferError as exc:
        print('self export', type(exc).__name__, value)
    else:
        print('missing self export exception')
    value.append(99)
    print('source lease released', value)

empty = bytearray()
print('empty self', empty.__iadd__(empty) is empty, empty)
value = bytearray(b'ab')
view = memoryview(value)
print('empty addition while exported', value.__iadd__(b'') is value)
for source in [b'c', view]:
    try:
        value += source
    except BufferError as exc:
        print('exported resize', type(exc).__name__, value)
    else:
        print('missing export exception')
view.release()
value += b'c'
print('released resize', value)

source = bytearray(b'y')
value = bytearray(b'x')
value += source
source.append(33)
print('temporary export released', value, source)

value = bytearray(b'ab')
exported = pickle.PickleBuffer(value)
try:
    value += exported
except BufferError as exc:
    print('picklebuffer self export', type(exc).__name__, value)
else:
    print('missing picklebuffer export exception')
exported.release()
value += b'c'
print('picklebuffer released resize', value)
