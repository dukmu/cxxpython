import re

print(re.findall(r'\w+', 'café 你好 42'))
print(re.findall(r'\w+', 'café 你好 42', re.ASCII))
print(re.findall(r'\d+', 'a١２3'))
print(re.findall(r'\s+', 'a\u2003b c'))
print([bool(re.fullmatch('[a-z]', c, re.I)) for c in 'iIİıſK'])
print([bool(re.fullmatch('σ', c, re.I)) for c in 'σςΣ'])
print([bool(re.fullmatch('[a-z]', c, re.I | re.ASCII)) for c in 'iIİıſK'])
print(re.findall(br'\w+', b'abc_42 \xff', re.L))
for data in (bytearray(b'abc123'), memoryview(b'abc123')):
    m = re.search(br'([a-z]+)(\d+)', data)
    print(m.group(), m.groups(), m.span(), m.string is data)
    print(re.findall(br'\d+', data))
for fn in (lambda: re.compile('a', re.L),
           lambda: re.compile(b'a', re.L | re.A),
           lambda: re.compile(b'a', re.U),
           lambda: re.search(b'a', 'a')):
    try:
        fn()
    except Exception as exc:
        print(type(exc).__name__)
