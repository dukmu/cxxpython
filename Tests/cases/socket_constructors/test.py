import socket
import _socket
import struct
import errno

blank = _socket.socket.__new__(_socket.socket)
print(blank.family, blank.type, blank.proto, blank.fileno(), blank.gettimeout())
blank.close()
previous = socket.getdefaulttimeout()
try:
    socket.setdefaulttimeout(0.25)
    sock = socket.socket()
    print(sock.family == socket.AF_INET, sock.type == socket.SOCK_STREAM,
          sock.proto, sock.gettimeout(), sock.getblocking())
    sock.close()
finally:
    socket.setdefaulttimeout(previous)
print(socket.getdefaulttimeout())

left, right = socket.socketpair()
try:
    fd = left.detach()
    print(left.fileno(), fd >= 0)
    adopted = socket.socket(fileno=fd)
    try:
        print(adopted.family == socket.AF_UNIX, adopted.type == socket.SOCK_STREAM)
        print(adopted.getsockopt(socket.SOL_SOCKET, socket.SO_TYPE) == socket.SOCK_STREAM)
        binary = adopted.getsockopt(socket.SOL_SOCKET, socket.SO_TYPE, 4)
        print(struct.unpack('i', binary)[0] == socket.SOCK_STREAM)
        right.sendall(b'adopt')
        print(adopted.recv(5))
    finally:
        adopted.close()
finally:
    left.close()
    right.close()

for operation in (lambda: socket.socket(fileno=-1),
                  lambda: socket.setdefaulttimeout(-1),
                  lambda: socket.setdefaulttimeout(float('nan'))):
    try:
        operation()
    except Exception as exc:
        print(type(exc).__name__)
print(issubclass(BrokenPipeError, ConnectionError), socket.timeout is TimeoutError)
for code in (errno.ECONNREFUSED, errno.ECONNRESET, errno.ECONNABORTED,
             errno.ETIMEDOUT, errno.EINPROGRESS):
    print(type(OSError(code, 'test')).__name__)
