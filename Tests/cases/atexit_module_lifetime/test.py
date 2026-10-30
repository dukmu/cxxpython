import atexit
import gc
import importlib
import sys
import weakref

class Callback:
    def __call__(self):
        print('retained exit callback')
callback = Callback()
reference = weakref.ref(callback)
atexit.register(callback)
before = atexit._ncallbacks()
importlib.reload(atexit)
print(atexit._ncallbacks() == before)
del callback
del sys.modules['atexit']
del atexit
gc.collect()
print(reference() is not None)
del sys.modules['sys']
