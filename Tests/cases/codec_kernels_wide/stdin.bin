import _codecs

print(_codecs.utf_16_le_encode('A\U0001f600'))
print(_codecs.utf_16_be_encode('A\U0001f600'))
print(_codecs.utf_32_le_encode('A\U0001f600'))
print(_codecs.utf_32_be_encode('A\U0001f600'))
print(_codecs.utf_16_ex_decode(b'\xff\xfeA\x00\x3d\xd8\x00\xde', 'strict', 0, True))
print(_codecs.utf_32_ex_decode(b'\x00\x00\xfe\xff\x00\x00\x00A', 'strict', 0, True))
print(_codecs.utf_16_ex_decode(b'A\x00\x3d\xd8', 'strict', -1, False))
print(_codecs.utf_32_ex_decode(b'A\x00\x00\x00\x01', 'strict', -1, False))
print(_codecs.utf_8_decode(b'A\xe2\x82', 'strict', False))
print(_codecs.utf_16_le_decode(b'\x00\xd8', 'surrogatepass', True))
print(_codecs.utf_32_be_encode('\udc80', 'surrogatepass'))
