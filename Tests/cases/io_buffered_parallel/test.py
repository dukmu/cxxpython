import _io
import _thread
import gc

class Raw(_io.BytesIO):
    def write(self, data):
        gc.collect()
        return super().write(data)

raw = Raw()
stream = _io.BufferedWriter(raw, 8)
gates = [_thread.allocate_lock(), _thread.allocate_lock()]
errors = []
for gate in gates:
    gate.acquire()

def worker(index, data):
    try:
        for i in range(80):
            stream.write(data)
            if i % 7 == 0:
                gc.collect()
    except BaseException as error:
        errors.append(type(error).__name__)
    finally:
        gates[index].release()

_thread.start_new_thread(worker, (0, b"a"))
_thread.start_new_thread(worker, (1, b"b"))
for gate in gates:
    gate.acquire()
stream.flush()
value = raw.getvalue()
a = 0
b = 0
for byte in value:
    if byte == 97:
        a += 1
    elif byte == 98:
        b += 1
print(errors, len(value), a, b)
