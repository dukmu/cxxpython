import io

for mode in ('', 'rr', 'rw', 'rbt', 'r++', 'U', 'z'):
    try:
        open('bad-file', mode)
    except ValueError:
        print('mode', repr(mode))
for kwargs in ({'mode': 'wb', 'encoding': 'utf-8'},
               {'mode': 'wb', 'errors': 'ignore'},
               {'mode': 'wb', 'newline': ''},
               {'mode': 'w', 'buffering': 0},
               {'mode': 'w', 'closefd': False}):
    try:
        open('bad-file', **kwargs)
    except ValueError:
        print('invalid combination')
try:
    io.open_code(b'bad-file')
except TypeError:
    print('code path type')
print(io.text_encoding('ascii'), io.text_encoding(None) is not None)
