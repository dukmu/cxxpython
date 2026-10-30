# CPython 3.12 test_unicode.py identifier behavior, including XID normalization.
for text in ['', '_', 'class', 'a1', '1a', 'a-b', '\u03b1\u0301',
             '\u0301a', '\u2118', '\u00b7', 'a\u00b7', '\uff3fa',
             '\U00010400x', '\ud800', 'a\u200c']:
    print(ascii(text), text.isidentifier())
print(str.isidentifier('name'), str.isidentifier('9name'))
try:
    str.isidentifier(42)
except TypeError as error:
    print(type(error).__name__)
