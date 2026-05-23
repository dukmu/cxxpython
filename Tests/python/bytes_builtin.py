empty = bytes()
text = bytes('abc', 'ascii')
items = bytes([65, 0, 255])
zeros = bytes(3)

print(type(empty))
print(repr(empty))
print(repr(text))
print(repr(items))
print(len(text))
print(text[0])
print(text[-1])
print(repr(text[1:]))
print(list(text))
print(bytes(text) == text)
print(bytes([65]) + bytes([66]) == bytes('AB', 'ascii'))
print(97 in text)
print(bytes('b', 'ascii') in text)
print(bytes('', 'ascii') in text)
print(len(zeros))
print(zeros[0] == 0)
print(text.decode() == 'abc')
print(text.decode('utf-8') == 'abc')
try:
    bytes('abc')
except TypeError as exc:
    print(type(exc))
try:
    bytes([256])
except ValueError as exc:
    print(type(exc))
try:
    text['x']
except TypeError as exc:
    print(type(exc))
