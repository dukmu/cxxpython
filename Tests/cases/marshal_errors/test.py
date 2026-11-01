import marshal

def outcome(fn, *args):
    try:
        fn(*args)
    except Exception as exc:
        print(type(exc).__name__)
    else:
        print('no error')

for data in (b'', b'i', b'i\x01\x00', b's\x03\x00\x00\x00a',
             b'?', b'r\x00\x00\x00\x00', b'\xff',
             b's\xff\xff\xff\xff', b'[\xff\xff\xff\xff',
             b'l\x01\x00\x00\x00\x00\x80'):
    outcome(marshal.loads, data)

outcome(marshal.dumps, object())
outcome(marshal.loads, 'not bytes')
print(marshal.loads(b'Ni\x01\x00\x00\x00') is None)

class IntSubclass(int):
    pass

class FloatSubclass(float):
    pass

class StrSubclass(str):
    pass

class ListSubclass(list):
    pass

class TupleSubclass(tuple):
    pass

class DictSubclass(dict):
    pass

class SetSubclass(set):
    pass

for value in (IntSubclass(1), FloatSubclass(1.0), StrSubclass('x'),
              ListSubclass(), TupleSubclass(), DictSubclass(), SetSubclass()):
    outcome(marshal.dumps, value)

outcome(marshal.dumps, memoryview(b'abc')[::2])
