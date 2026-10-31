import gc
import io
import pickle


class External:
    def __init__(self, key):
        self.key = key


class Writer:
    def __init__(self):
        self.stream = io.BytesIO()
        self.calls = 0

    def write(self, data):
        gc.collect()
        self.calls += 1
        return self.stream.write(data)


class Reader:
    def __init__(self, data):
        self.stream = io.BytesIO(data)

    def read(self, size=-1):
        gc.collect()
        return self.stream.read(size)

    def readline(self):
        gc.collect()
        return self.stream.readline()


class Pickler(pickle.Pickler):
    def persistent_id(self, value):
        if isinstance(value, External):
            gc.collect()
            return value.key
        return None


class Unpickler(pickle.Unpickler):
    def persistent_load(self, key):
        gc.collect()
        if key not in objects:
            raise pickle.UnpicklingError('unknown external key')
        return objects[key]


external = External('row-7')
objects = {'row-7': external}
for protocol in (2, 4, 5):
    writer = Writer()
    Pickler(writer, protocol=protocol).dump([external, external, 'local'])
    encoded = writer.stream.getvalue()
    result = Unpickler(Reader(encoded)).load()
    assert result[0] is result[1] is external and result[2] == 'local'
    assert writer.calls > 0
    try:
        pickle.loads(encoded)
    except pickle.UnpicklingError:
        print('persistent roundtrip', protocol, True)
    else:
        raise AssertionError('missing persistent_load accepted')

writer = Writer()
Pickler(writer, protocol=4).dump(External('missing'))
try:
    Unpickler(Reader(writer.stream.getvalue())).load()
except pickle.UnpicklingError as exc:
    print('callback error', str(exc))
else:
    raise AssertionError('unknown persistent id accepted')

writer = Writer()
Pickler(writer, protocol=0).dump([external, external, 'local'])
result = Unpickler(Reader(writer.stream.getvalue())).load()
assert result[0] is result[1] is external and result[2] == 'local'
print('persistent roundtrip', 0, True)
