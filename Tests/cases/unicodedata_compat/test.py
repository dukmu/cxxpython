import unicodedata as u

old = u.ucd_3_2_0
print(old.unidata_version, type(old).__name__, type(old).__module__)
for char in ('A', '\u00b2', '\u0f33', '\u20ac', '\u03f9', '\u1d2c',
             '\u1e9b', '\U0001f600', '\U0002f868', '\U000f0000'):
    print(ascii(char), old.decimal(char, None), old.digit(char, None),
          old.numeric(char, None), old.category(char), old.bidirectional(char),
          old.combining(char), old.mirrored(char), old.east_asian_width(char),
          old.decomposition(char), old.name(char, None))
for database in (u, old):
    for name in ('LATIN SMALL LETTER A', 'LINE FEED', 'KEYCAP NUMBER SIGN',
                 'ARABIC SEQUENCE NOON WITH KEHEH', 'HANGUL SYLLABLE GA',
                 'hangul syllable ga', 'CJK UNIFIED IDEOGRAPH-4E2D'):
        try:
            print(database.unidata_version, name, ascii(database.lookup(name)))
        except KeyError as exc:
            print(database.unidata_version, name, type(exc).__name__, str(exc))
    for form in ('NFC', 'NFD', 'NFKC', 'NFKD'):
        for text in ('\U0002f868', '\u1e9b\u0323', '\u1100\u1161\u11a8'):
            print(database.unidata_version, form, ascii(database.normalize(form, text)))
    print(database.normalize('BAD', ''), database.is_normalized('BAD', ''))

for call in (lambda: u.category(3), lambda: u.name('ab'),
             lambda: u.decimal('x', None, None), lambda: u.normalize(3, 'a'),
             lambda: u.is_normalized('NFC', 3), lambda: u.lookup(3),
             lambda: u.lookup(bytearray(b'X')), lambda: u.lookup('x' * 257),
             lambda: u.lookup('missing\0name'), lambda: u.lookup('\ud800')):
    try:
        call()
    except Exception as exc:
        print(type(exc).__name__, ascii(str(exc)))
print(ascii(u.lookup(b'LATIN CAPITAL LETTER A')))

for operation in (lambda: type(old)(),
                  lambda: setattr(type(old), '__new__', object.__new__)):
    try:
        operation()
    except TypeError as exc:
        print(type(exc).__name__)
    else:
        raise AssertionError('UCD construction and mutation must be rejected')
