import gc

print((65).to_bytes(), int.from_bytes(b'\x41'))
for n in (0, 1, 127, 128, 255, 256, -1, -128, -129, 2**130 + 123):
    for size in (0, 1, 2, 18):
        for signed in (False, True):
            try:
                big = n.to_bytes(size, 'big', signed=signed)
                little = n.to_bytes(size, 'little', signed=signed)
                print(n, size, signed, big.hex(), little.hex(),
                      int.from_bytes(big, signed=signed),
                      int.from_bytes(little, 'little', signed=signed))
            except Exception as e:
                print(n, size, signed, type(e).__name__, str(e))

class Index:
    def __index__(self):
        gc.collect()
        print('index')
        return 2

class Signed:
    def __bool__(self):
        gc.collect()
        print('bool')
        return True

class Source:
    def __bytes__(self):
        gc.collect()
        print('bytes')
        return b'\xff\x80'
    def __iter__(self):
        print('iter')
        return iter([1])

class Child(int):
    def __new__(cls, value):
        gc.collect()
        print('child', value)
        return int.__new__(cls, value)

class IndexIterable:
    def __index__(self):
        return 10
    def __iter__(self):
        return iter([1, 2])

print((258).to_bytes(Index(), signed=Signed()).hex())
print(int.from_bytes(Source(), signed=Signed()))
print(int.from_bytes(IndexIterable()))
print(int.from_bytes([0, Index(), 255]))
print(int.from_bytes(bytearray(b'\x01\x02')))
print(int.from_bytes(memoryview(bytearray(b'\x01\x02'))))
print(int.from_bytes(memoryview(b'\x01\x02\x03')[::2]))
print(type(Child.from_bytes(b'\x03')).__name__)
print(int.from_bytes(b'\x80', signed=True))

def check(f):
    try:
        print(f())
    except Exception as e:
        print(type(e).__name__, str(e))

check(lambda: (1).to_bytes(-1))
check(lambda: (1).to_bytes(1 << 100))
check(lambda: (1).to_bytes(1, 'middle', signed=Signed()))
check(lambda: int.from_bytes(b'', 'middle', signed=Signed()))
check(lambda: (1).to_bytes(1, None))
check(lambda: int.from_bytes(b'', None))
check(lambda: int.from_bytes([256]))
check(lambda: int.from_bytes([-1]))
check(lambda: int.from_bytes('x'))
check(lambda: int.from_bytes(5))

class BadBytes:
    def __bytes__(self):
        gc.collect()
        return 12

class Broken:
    def __iter__(self):
        gc.collect()
        raise RuntimeError('iteration')

check(lambda: int.from_bytes(BadBytes()))
check(lambda: int.from_bytes(Broken()))

data = bytes(range(256)) * 256
for order in ('big', 'little'):
    value = int.from_bytes(data, order)
    print(value.to_bytes(len(data), order) == data)
