import gc
import pickle
import socket

left, right = socket.socketpair()
left.settimeout(2)
right.settimeout(2)
try:
    data = bytearray(b'.....')
    view = memoryview(data)[1:4]

    class Flags:
        def __index__(self):
            gc.collect()
            try:
                view.release()
            except BufferError:
                print('export held during callback')
            return 0

    right.sendall(pickle.PickleBuffer(b'abc'))
    print(left.recv_into(view, 3, Flags()), bytes(data))
    view.release()
    data.extend(b'!')
    print(bytes(data))
    print(left.recv(0), left.recv_into(bytearray(), 0))
    for operation in (lambda: left.recv(-1),
                      lambda: left.recv((1 << 63) - 1),
                      lambda: left.recv_into(b'readonly'),
                      lambda: left.recv_into(bytearray(1), 2),
                      lambda: right.send('text'),
                      lambda: right.send(memoryview(b'abc')[::2])):
        try:
            operation()
        except Exception as exc:
            print(type(exc).__name__)
finally:
    left.close()
    right.close()
