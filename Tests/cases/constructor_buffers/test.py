import gc

for value in (b'101', bytearray(b'101'), memoryview(b'101')):
    print(str(value, 'ascii'), int(value), int(value, 2) if not isinstance(value, memoryview) else 'no base')
print(str(bytearray(b'caf\xe9'), 'latin1'))
print(str(memoryview(b'a\xff'), 'ascii', 'replace'))

class Text(str):
    pass

class Data(bytearray):
    def __int__(self):
        gc.collect()
        return 99

print(type(Text(b'ab', 'ascii')).__name__, Text(b'ab', 'ascii'))
print(int(Data(b'11')), int(Data(b'11'), 2))
for action in (lambda: int(b'bad', 2), lambda: int(b'\xa01'),
               lambda: int(bytearray(b'12_')), lambda: int(memoryview(b'10'), 2),
               lambda: str('x', 'ascii'), lambda: str(b'\xff', 'ascii')):
    try:
        action()
    except Exception as exc:
        print(type(exc).__name__)
for start, end in ((0, 6), (-4, 6), (8, 10), (4, 2), (-100, 100)):
    print('ababa'.count('ab', start, end), 'ababa'.count('', start, end))
print('aaaa'.count('aa'), '你好你好'.count('你好'))
