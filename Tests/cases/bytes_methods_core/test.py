for factory in (bytes, bytearray):
    data = factory(b'  ab--ab--c  ')
    print(data.find(b'ab'), data.rfind(b'ab'), data.index(97), data.rindex(b'--'))
    print(data.count(b'ab'), data.count(b''), data.find(b'', 100), data.rfind(b'', 1, 3))
    print(data.find(b'ab', -100, 10**100), data.find(b'ab', 10**100))
    print(data.partition(b'--'), data.rpartition(b'--'), data.partition(b'xyz'))
    print(data.strip(), data.lstrip(), data.rstrip(), data.strip(b' c'))
    print(data.split(b'--'), data.rsplit(b'--', 1))
    print(data.split(), data.split(None, 0), data.rsplit(None, 0))
    print(factory(b'  a b  ').split(None, 1), factory(b'  a b  ').rsplit(None, 1))
    print(factory(b'aaa').split(b'a'), factory(b'aaa').rsplit(b'a', 2))
    for operation in (lambda: data.index(b'!'), lambda: data.split(b''),
                      lambda: data.find(256), lambda: data.split(None, 10**100),
                      lambda: data.split(None, None)):
        try:
            operation()
        except Exception as exc:
            print(type(exc).__name__)


class Needle:
    def __index__(self):
        return 98


print(b'abc'.find(Needle()), b'abc'.count(Needle()))
print(b'abcabc'.find(memoryview(b'bc')), bytearray(b'--abc--').strip(memoryview(b'-')))
