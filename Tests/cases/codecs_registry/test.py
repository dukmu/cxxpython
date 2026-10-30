import codecs
import encodings
import gc

for name in ('utf-8', 'UTF_8', 'us-ascii', 'latin1', 'utf-16', 'utf-32', 'cp1252'):
    info = codecs.lookup(name)
    print(name, info.name, len(info), callable(info.encode), callable(info.decode))
print(codecs.lookup('utf-8') is codecs.lookup('UTF 8'))
print(encodings.normalize_encoding(' A--B...c\u00e9d '))

calls = []
def encode(value, errors='strict'):
    return value.upper().encode('ascii'), len(value)
def decode(value, errors='strict'):
    return bytes(value).decode('ascii').lower(), len(value)
def search(name):
    gc.collect()
    calls.append(name)
    if name == 'example_codec':
        return codecs.CodecInfo(encode, decode, name='example-codec')
codecs.register(search)
info = codecs.lookup('Example-Codec')
print(info.name, codecs.encode('abc', 'example-codec'), codecs.decode(b'ABC', 'example-codec'))
print('abc'.encode('example-codec'), b'ABC'.decode('example-codec'))
print(codecs.lookup('example-codec') is info, calls)
codecs.unregister(search)
try:
    codecs.lookup('example-codec')
except LookupError:
    print('unregistered')

def handler(error):
    gc.collect()
    return '<bad>', error.end
codecs.register_error('user-handler', handler)
print(codecs.lookup_error('user-handler') is handler)
print(b'bad\xff'.decode('utf8', 'user-handler'))
print('bad\u20ac'.encode('ascii', 'user-handler'))

class NativeAliases:
    size = len
    encode = codecs.utf_8_encode
print(NativeAliases().size([1, 2]))
print(NativeAliases().encode('abc'))
from encodings.ascii import Codec
print(Codec().encode('abc'), Codec().decode(b'abc'))
