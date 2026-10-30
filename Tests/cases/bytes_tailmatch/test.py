for receiver in (b'abcabc', bytearray(b'abcabc')):
    for part in (b'', b'a', b'bc', b'abc', bytearray(b'a'), memoryview(b'bc'), (b'x', b'a')):
        for first, last in ((0, None), (-3, -1), (9, 20), (0, -100), (3, 6)):
            print(receiver.startswith(part, first, last), receiver.endswith(part, first, last))
    try:
        receiver.startswith(3)
    except TypeError:
        print('type required')
