import _codecs
import gc
import pickle
import threading

errors = []

class Keywords:
    def __init__(self, *, label):
        self.label = label
    def __getnewargs_ex__(self):
        gc.collect()
        return (), {'label': self.label}

def worker():
    try:
        for i in range(6):
            protocol = i % 3
            assert pickle.loads(pickle.dumps(_codecs.encode, protocol)) is _codecs.encode
            assert pickle.loads(pickle.dumps(b'\x00\xff', protocol)) == b'\x00\xff'
            value = pickle.loads(pickle.dumps(Keywords(label=i), 2))
            assert value.label == i
            assert list.append.__qualname__ == 'list.append'
            assert [1].append.__qualname__ == 'list.append'
            gc.collect()
    except Exception as error:
        errors.append(type(error).__name__)

threads = [threading.Thread(target=worker) for i in range(3)]
for thread in threads:
    thread.start()
for thread in threads:
    thread.join()
print(errors)

original = len.__module__
def metadata_worker():
    try:
        for i in range(6):
            len.__module__ = {'metadata': [i]}
            bound = [].append
            bound.__module__ = {'bound': [i]}
            gc.collect()
            assert isinstance(len.__module__, dict)
            assert bound.__module__ == {'bound': [i]}
            assert len.__self__.__name__ == 'builtins'
            assert len([1, 2]) == 2
    except Exception as error:
        errors.append(type(error).__name__)

try:
    threads = [threading.Thread(target=metadata_worker) for i in range(3)]
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join()
finally:
    len.__module__ = original
print(errors, len.__module__)
