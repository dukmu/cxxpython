import io
import linecache
import os
import tokenize

for data in (b'print(1)\n', b'# coding: latin-1\nname = "\xe9"\n',
             b'\xef\xbb\xbf# coding: utf-8\n', b'#!/usr/bin/python\n# coding: latin-1\n', b''):
    print(tokenize.detect_encoding(io.BytesIO(data).readline))
for data in (b'# coding: unknown-codec\n', b'\xef\xbb\xbf# coding: latin-1\n', b'\xff\n'):
    try:
        tokenize.detect_encoding(io.BytesIO(data).readline)
    except SyntaxError as exc:
        print(type(exc).__name__)

path = 'encoded_source.py'
try:
    with open(path, 'wb') as stream:
        stream.write(b'# coding: latin-1\nvalue = "\xe9"\n')
    with tokenize.open(path) as source:
        print(source.encoding, source.mode, source.read().splitlines())
    print(linecache.getline(path, 2).rstrip(), linecache.getline(path, 3))
    linecache.checkcache(path)
    print(linecache.getlines(path))
    with open(path, 'wb') as stream:
        stream.write(b'value = 12345\n')
    linecache.checkcache(path)
    print(linecache.getline(path, 1).rstrip())
finally:
    os.unlink(path)
linecache.checkcache(path)
print(linecache.getline(path, 1), path not in linecache.cache)

class Loader:
    def get_source(self, name):
        print('load', name)
        return 'first\nsecond'

namespace = {'__name__': 'virtual_source', '__loader__': Loader()}
print(linecache.lazycache('virtual_source.py', namespace))
print(linecache.getlines('virtual_source.py', namespace))
linecache.clearcache()
print(not linecache.cache)
