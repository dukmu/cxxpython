import socket

left, right = socket.socketpair()
try:
    print(left.family == socket.AF_UNIX, left.type == socket.SOCK_STREAM)
    print(left.gettimeout(), left.getblocking())
    left.setblocking(False)
    print(left.gettimeout(), left.getblocking())
    try:
        left.recv(1)
    except BlockingIOError:
        print('would block')
    left.settimeout(1.5)
    print(left.gettimeout(), left.getblocking())
    left.settimeout(0.01)
    try:
        left.recv(1)
    except TimeoutError:
        print('timed out')
    left.settimeout(None)
    print(left.gettimeout(), left.getblocking())

    print(right.send(memoryview(b'abcdef')))
    print(left.recv(2, socket.MSG_PEEK), left.recv(2))
    buf = bytearray(6)
    print(left.recv_into(memoryview(buf)[1:5]), bytes(buf))

    duplicate = left.dup()
    left.close()
    right.sendall(bytearray(b'dup'))
    print(duplicate.recv(3), left.fileno() == -1)
    duplicate.close()
    try:
        duplicate.recv(1)
    except OSError:
        print('closed')
finally:
    left.close()
    right.close()

left, right = socket.socketpair()
try:
    stream = left.makefile('rwb')
    left.close()
    stream.write(b'hello\n')
    stream.flush()
    print(right.recv(6))
    right.sendall(b'world\n')
    print(stream.readline())
    stream.close()
finally:
    left.close()
    right.close()
