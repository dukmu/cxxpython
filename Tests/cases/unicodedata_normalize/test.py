import unicodedata as u

samples = ('A\u030a', '\u212b', '\ufb01', '\u1e69', '\u1100\u1161\u11a8',
           'a\u0315\u0300\u05ae', '\u1e9b\u0323', '\uac01', '\ud800',
           '\ufdfa', 'x' + '\u0315\u0300' * 100)
for form in ('NFC', 'NFD', 'NFKC', 'NFKD'):
    for text in samples:
        result = u.normalize(form, text)
        print(form, ascii(result), u.is_normalized(form, text),
              u.is_normalized(form, result))
try:
    u.normalize('invalid', 'x')
except ValueError:
    print('invalid form')
