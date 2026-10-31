import gc
import os
import socket
import weakref

def is_closed(fd):
    try:
        os.fstat(fd)
    except OSError:
        return True
    return False

sock = socket.socket()
fd = sock.fileno()
ref = weakref.ref(sock)
del sock
gc.collect()
gc.collect()
print(ref() is None, is_closed(fd))

saved = []
class Resurrecting(socket.socket):
    def __del__(self):
        saved.append(self)

sock = Resurrecting()
fd = sock.fileno()
del sock
gc.collect()
print(len(saved), is_closed(fd), saved[0].fileno() == fd)
saved[0].close()
saved.clear()
gc.collect()
gc.collect()
print(is_closed(fd))

sock = socket.socket()
old_fd = sock.fileno()
sock.close()
print(is_closed(old_fd))
sock.__init__()
print(sock.family == socket.AF_INET, sock.type == socket.SOCK_STREAM)
sock.close()
sock.close()
print(sock.fileno())
