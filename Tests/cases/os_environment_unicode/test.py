import os

values = [b'utf8-\xc3\xa9', b'bad-\xff', b'truncated-\xe2\x82',
          b'surrogate-\xed\xa0\x80', b'overlong-\xc0\xaf']
for value in values:
    decoded = os.fsdecode(value)
    print(os.fsencode(decoded) == value)
    os.environb[b'CXXPYTHON_TEST_\xff'] = value
    print(os.environb[b'CXXPYTHON_TEST_\xff'] == value)
    print(os.environ['CXXPYTHON_TEST_\udcff'] == decoded)
    del os.environ['CXXPYTHON_TEST_\udcff']
    print(b'CXXPYTHON_TEST_\xff' not in os.environb)
