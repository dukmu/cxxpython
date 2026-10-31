import struct
import gc

for fmt, values in (
    ('b', [-2, 0, 127]), ('B', [0, 128, 255]),
    ('h', [-123, 0, 32767]), ('H', [0, 1, 65535]),
    ('i', [-123, 0, 2147483647]), ('I', [0, 1, 4294967295]),
    ('l', [-123, 0, 456]), ('L', [0, 1, 456]),
    ('q', [-123, 0, 9223372036854775807]), ('Q', [0, 1, 18446744073709551615]),
    ('n', [-123, 0, 456]), ('N', [0, 1, 456]), ('P', [0, 1, 456]),
    ('e', [-1.5, 0.0, 2.25]), ('f', [-1.5, 0.0, 2.25]), ('d', [-1.5, 0.0, 2.25]),
    ('?', [False, True, False]), ('c', [b'a', b'\x00', b'z']),
):
    raw = bytearray(struct.pack('@' + fmt * 3, *values))
    view = memoryview(raw).cast(fmt)
    print(fmt, view.format, view.itemsize == struct.calcsize('@' + fmt),
          view.nbytes == len(raw), view.shape, view.strides == (view.itemsize,), view.tolist())
    view[1] = values[0]
    values[1] = values[0]
    print(view.tolist(), view[::-1].tolist(),
          view[::-1].tobytes() == struct.pack('@' + fmt * 3, *values[::-1]),
          view.cast('B').tobytes() == bytes(raw))
    other = memoryview(bytes(raw)).cast('@' + fmt)
    print('equivalent', view == other, other.format, other.readonly)
    view.release()
    other.release()

view = memoryview(bytearray(12)).cast('i')
view[:] = memoryview(struct.pack('@iii', 1, 2, 3)).cast('i')
print('slices', view.tolist(), view[::2].tolist(), list(iter(view)))
view[::2] = memoryview(struct.pack('@ii', 8, 9)).cast('i')
print('writes', view.tolist(), view == memoryview(struct.pack('@hhh', 8, 2, 9)).cast('h'))
print('empty', memoryview(b'').cast('I').tolist())

class Number:
    def __index__(self):
        gc.collect()
        return 42

class Real:
    def __float__(self):
        gc.collect()
        return 1.25

view[1] = Number()
floating = memoryview(bytearray(8)).cast('d')
floating[0] = Real()
print('callbacks', view[1], floating.tolist())
