for s in ('', 'abc123', 'é', '中', '²', '½', '١', '\u2003', '\x1c', '\ud800'):
    print(ascii(s), s.isascii(), s.isalnum(), s.isalpha(), s.isspace(), s.isdecimal(), s.isdigit(), s.isnumeric())
for s in ('Straße', 'İ', 'ΟΣ', 'ΟΣΑ', 'ΟΣ\u0301', 'ﬃ', 'ǰ'):
    print(ascii(s.lower()), ascii(s.upper()), ascii(s.casefold()))
for s in ('', 'abc', 'aaa'):
    print(ascii(s.replace('', '-', 2)), ascii(s.replace('a', 'XY')), ascii(s.replace('a', '', 0)))
print(ascii('\u2003 hi \x1c'.strip()), 'xyhelloxy'.strip('xy'), 'xyhelloxy'.lstrip('xy'), 'xyhelloxy'.rstrip('xy'))
print('a--b--c'.partition('--'), 'a--b--c'.rpartition('--'), 'abc'.partition('x'), 'abc'.rpartition('x'))
print('a\r\nb\rc\nd\x85e\u2028f\u2029'.splitlines())
print('a\r\nb\rc\nd\x85e\u2028f\u2029'.splitlines(True))
print(''.splitlines(), '\n'.splitlines(), 'x\n'.splitlines())
for f in (lambda: 'x'.partition(''), lambda: 'x'.replace(1, ''), lambda: 'x'.strip(1)):
    try:
        f()
    except Exception as e:
        print(type(e).__name__)
