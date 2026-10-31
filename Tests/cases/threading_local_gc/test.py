import gc
import threading
import weakref
from _threading_local import local

# Derived from CPython 3.12.14 test_threading_local's subclass and local-ref
# tests, explicitly exercising the upstream pure-Python fallback implementation.
initialized = []
errors = []


class Local(local):
    def __init__(self, value):
        initialized.append(threading.current_thread().name)
        self.value = value


class Payload:
    pass


data = Local("initial")
data.value = "main"
refs = []
def worker():
    try:
        assert data.value == "initial"
        data.value = "worker"
        payload = Payload()
        payload.cycle = payload
        refs.append(weakref.ref(payload))
        data.payload = payload
    except BaseException as exc:
        errors.append(type(exc).__name__)

t = threading.Thread(target=worker, name="local-worker")
t.start()
t.join(5)
assert not t.is_alive() and not errors
del t
assert data.value == "main"
gc.collect()
print("values", data.value, initialized)
print("thread local collected", refs[0]() is None)
