# Adapted from CPython 3.12 string_tests.py:test_mul and
# test_unicode.py:test_repeat_id_preserving.
for count in [-3, 0, 1, 4, False, True]:
    print(count, repr('abc' * count), repr(count * 'abc'))
print(repr('' * 4), repr(4 * ''))
print(ascii(('a\u03b1\U0001f600\ud800') * 2))
print(str.__mul__('ab', 3), str.__rmul__('ab', 3))
text = '123abc1@'
print(text * 1 is text, 1 * text is text, text * 2 is text)

class SubStr(str):
    def __len__(self):
        return 999
    def __str__(self):
        return 'not the payload'

text = SubStr('ab')
for count in [-1, 0, 1, 3]:
    result = text * count
    print(repr(result), type(result) is str, result is text)
    result = count * text
    print(repr(result), type(result) is str, result is text)
text = 'abc' * 2000
print(len(text), text[:6], text[-6:])
