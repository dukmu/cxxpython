import gc

table = bytes.maketrans(b'ab', b'XY')
print(len(table), table[ord('a')], table[ord('b')])
print(b'abcz'.translate(table), b'abcz'.translate(None, delete=b'bz'))
print(bytearray(b'abcz').translate(table), bytearray(b'abcz').translate(None, b'bz'))
print(bytes.maketrans(b'a', b'x') == bytearray.maketrans(b'a', b'x'))
print(b'abc'.translate(memoryview(table), delete=bytearray(b'b')))
source = b'abc'
print(source.translate(None), source.translate(None) is source)

class Bytes(bytes):
    pass

class Mutable(bytearray):
    pass

print(type(Bytes(b'abc').translate(None)).__name__,
      type(Mutable(b'abc').translate(None)).__name__)
for action in (lambda: b'a'.translate(bytes(255)), lambda: bytearray(b'a').translate(b''),
               lambda: bytes.maketrans(b'a', b'bc'), lambda: b'a'.translate('x'),
               lambda: bytearray(b'a').translate(None, None)):
    try:
        action()
    except Exception as exc:
        print(type(exc).__name__)
gc.collect()
print(bytearray(range(256)).translate(table)[97:100])
