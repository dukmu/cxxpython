import gc
import _io as io
import _weakref

stream = io.BytesIO(b"abc")
reference = _weakref.ref(stream)
view = stream.getbuffer()
child = view[1:]
del stream
gc.collect()
print(reference() is not None, view.tobytes())
view.release()
gc.collect()
print(reference() is not None, child.tobytes())
child.release()
gc.collect()
print(reference() is None)
