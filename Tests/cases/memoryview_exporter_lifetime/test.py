import gc
import _weakref as weakref

class Exporter(bytearray):
    pass

exporter = Exporter(b"abc")
reference = weakref.ref(exporter)
view = memoryview(exporter)
child = view[1:]
del exporter
gc.collect()
print(reference() is not None)
view.release()
gc.collect()
print(reference() is not None, child.tobytes())
child.release()
gc.collect()
print(reference() is None)
