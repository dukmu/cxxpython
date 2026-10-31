import unicodedata as u

print(u.unidata_version)
for c in ('A', '9', '\u0662', '\u00b2', '\u00bd', '\u0f33', '\u0301',
          '\u05d0', '\u4e2d', '\U0001f600', '\u0378', '\ud800'):
    print(ascii(c), u.decimal(c, None), u.digit(c, None), u.numeric(c, None),
          u.category(c), u.bidirectional(c), u.combining(c), u.mirrored(c),
          u.east_asian_width(c), u.decomposition(c), u.name(c, None))
for name in ('latin small letter a', 'HANGUL SYLLABLE GA',
             'CJK UNIFIED IDEOGRAPH-4E2D', 'LINE FEED'):
    print(name, ascii(u.lookup(name)))
for function, arg in ((u.decimal, 'A'), (u.digit, 'A'), (u.numeric, 'A'),
                      (u.name, '\u0378'), (u.lookup, 'not a character')):
    try:
        function(arg)
    except (ValueError, KeyError) as exc:
        print(type(exc).__name__)
