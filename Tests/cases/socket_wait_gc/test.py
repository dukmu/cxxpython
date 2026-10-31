import gc
import socket
import threading

left, right = socket.socketpair()
left.settimeout(2)
right.settimeout(2)
ready = threading.Event()
result = []

def reader():
    ready.set()
    result.append(left.recv(4))

thread = threading.Thread(target=reader, daemon=True)
try:
    thread.start()
    assert ready.wait(2)
    gc.collect()
    right.sendall(b'wake')
    thread.join(3)
    assert not thread.is_alive()
    print(result)
finally:
    left.close()
    right.close()
