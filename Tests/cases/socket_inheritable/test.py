import os
import socket

sock = socket.socket()
duplicate = None
from_fd = None
left = None
right = None
try:
    duplicate = sock.dup()
    from_fd = socket.fromfd(sock.fileno(), sock.family, sock.type, sock.proto)
    left, right = socket.socketpair()
    sockets = (sock, duplicate, from_fd, left, right)
    print([item.get_inheritable() for item in sockets])
    print([os.get_inheritable(item.fileno()) for item in sockets])

    sock.set_inheritable(True)
    print(sock.get_inheritable(), os.get_inheritable(sock.fileno()),
          duplicate.get_inheritable())
    os.set_inheritable(sock.fileno(), False)
    print(sock.get_inheritable(), os.get_inheritable(sock.fileno()))

    fd = from_fd.fileno()
    from_fd.close()
    for operation in (lambda: from_fd.get_inheritable(),
                      lambda: from_fd.set_inheritable(True),
                      lambda: os.get_inheritable(fd),
                      lambda: os.set_inheritable(fd, True)):
        try:
            operation()
        except OSError:
            print('closed descriptor')
finally:
    for item in (sock, duplicate, from_fd, left, right):
        if item is not None:
            item.close()
