import base64
import io


for value in (b'', b'f', b'fo', b'foo', b'foob', b'fooba', b'foobar'):
    print('vector', value)
    for encode, decode in ((base64.b16encode, base64.b16decode),
                           (base64.b32encode, base64.b32decode),
                           (base64.b32hexencode, base64.b32hexdecode),
                           (base64.b64encode, base64.b64decode)):
        encoded = encode(value)
        print(encoded, decode(encoded))

data = b'\xfb\xff\x00hello'
print('alphabets', base64.standard_b64encode(data),
      base64.urlsafe_b64encode(data), base64.b64encode(data, b'._'))
print('decoded alphabets', base64.standard_b64decode(base64.standard_b64encode(data)),
      base64.urlsafe_b64decode(base64.urlsafe_b64encode(data)),
      base64.b64decode(base64.b64encode(data, b'._'), '._'))
print('base32 options', base64.b32decode('mzxw6===', casefold=True),
      base64.b32decode('MZXW6===', map01='L'),
      base64.b32decode('0I======', map01='I'),
      base64.b32hexdecode('cpnmu===', casefold=True))
print('base16 casefold', base64.b16decode('666f6f', casefold=True))

for value in (bytearray(b'hello'), memoryview(b'hello')):
    print('buffer', base64.b16encode(value), base64.b32encode(value),
          base64.b64encode(value), base64.a85encode(value), base64.b85encode(value))
    print('buffer decode', base64.b16decode(memoryview(b'6869')),
          base64.b32decode(bytearray(b'NBUQ====')),
          base64.b64decode(memoryview(b'aGk=')),
          base64.a85decode(bytearray(b'BP@')),
          base64.b85decode(memoryview(b'XlV')))

print('ascii85', base64.a85encode(b'\0\0\0\0    hello'),
      base64.a85encode(b'\0\0\0\0    hello', foldspaces=True, wrapcol=5, adobe=True),
      base64.a85encode(b'abc', pad=True))
print('ascii85 decode', base64.a85decode(b'<~zy BOu!rDZ~>', foldspaces=True, adobe=True),
      base64.a85decode(b'BOu!r\nDZ', ignorechars=b'\n'))
print('base85', base64.b85encode(b'hello'), base64.b85encode(b'abc', pad=True),
      base64.b85decode(base64.b85encode(b'hello')),
      base64.b85decode(base64.b85encode(b'abc', pad=True)))

def failure(label, function, *args, **kwargs):
    try:
        print(label, function(*args, **kwargs))
    except Exception as exc:
        print(label, type(exc).__name__, str(exc))


failure('base64 padding', base64.b64decode, b'a')
failure('base64 strict', base64.b64decode, b'aG!k=', validate=True)
print('base64 permissive', base64.b64decode(b'aG!k='))
failure('base64 alt length', base64.b64encode, b'x', b'!')
failure('non-ascii', base64.b64decode, '\u00e9')
failure('wrong type', base64.b64decode, 123)
failure('base16 digit', base64.b16decode, b'GG')
failure('base16 lowercase', base64.b16decode, b'ff')
failure('base32 padding', base64.b32decode, b'MZXW6')
failure('base32 digit', base64.b32decode, b'!!!!!!!!')
failure('base32hex digit', base64.b32hexdecode, b'WWWWWWWW')
failure('ascii85 digit', base64.a85decode, b'v')
failure('ascii85 adobe', base64.a85decode, b'hello', adobe=True)
failure('base85 digit', base64.b85decode, b'~ ')

source = b'abcdefghijklmnopqrstuvwxyz' * 4
encoded_stream = io.BytesIO()
base64.encode(io.BytesIO(source), encoded_stream)
print('stream encoded', encoded_stream.getvalue())
decoded_stream = io.BytesIO()
base64.decode(io.BytesIO(encoded_stream.getvalue()), decoded_stream)
print('stream decoded', decoded_stream.getvalue())
print('legacy bytes', base64.encodebytes(b'hello'), base64.decodebytes(b'aGVsbG8=\n'))
