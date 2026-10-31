import gc
import select
import socket

left, right = socket.socketpair()

class Descriptor:
    def __init__(self, fd):
        self.fd = fd

    def fileno(self):
        gc.collect()
        return self.fd

try:
    first = Descriptor(left.fileno())
    second = Descriptor(left.fileno())
    right.sendall(b'x')
    reads, writes, errors = select.select((item for item in (first, second, first)),
                                          (item for item in (right,)), (), 0)
    print(len(reads), reads[0] is first, reads[1] is second, reads[2] is first)
    print(len(writes), writes[0] is right, errors)
    print(left.recv(1))
    print(select.select([], [], [], 0))
    print(select.select([], [], [], 0.0))

    class Zero:
        def __index__(self):
            return 0

    print(select.select([], [], [], Zero()))
finally:
    left.close()
    right.close()
