import _codecs
import functools
import gc
import pickle

class Keywords:
    def __init__(self, *, label):
        self.label = label
    def __getnewargs_ex__(self):
        gc.collect()
        return (), {'label': self.label}

for protocol in range(6):
    encode = pickle.loads(pickle.dumps(_codecs.encode, protocol))
    print(protocol, encode is _codecs.encode, encode('text', 'ascii'))
    print(pickle.loads(pickle.dumps(b'\x00\xffbytes', protocol)))
    print(pickle.loads(pickle.dumps(len, protocol)) is len)
    new = pickle.loads(pickle.dumps(object.__new__, protocol))
    print(new.__self__ is object, new.__name__)
    append = pickle.loads(pickle.dumps([1, 2].append, protocol))
    append(3)
    print(append.__self__)
    descriptor = pickle.loads(pickle.dumps(list.append, protocol))
    print(descriptor is list.append)
    restored = pickle.loads(pickle.dumps(Keywords(label='keyword'), protocol))
    print(type(restored) is Keywords, restored.label)

for protocol in [2, 3]:
    constructor = functools.partial(Keywords.__new__, Keywords, label='keyword')
    restored = pickle.loads(pickle.dumps(constructor, protocol))
    print(protocol, restored.func.__self__ is object, type(restored()) is Keywords)
