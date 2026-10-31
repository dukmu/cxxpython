import select
import socket

left, right = socket.socketpair()
try:
    reads, writes, errors = select.select([left], [right], [left], 0)
    print(reads == [], writes == [right], errors == [])
    right.sendall(b'x')
    reads, writes, errors = select.select([left, left], [], [], 0)
    print(reads == [left, left], writes == [], errors == [])
    print(left.recv(1))
    print(select.select([], [], [], 0))
    try:
        select.select([], [], [], -1)
    except ValueError:
        print('negative timeout')
    fd = left.fileno()
    left.close()
    try:
        select.select([fd], [], [], 0)
    except OSError:
        print('bad descriptor')
finally:
    left.close()
    right.close()
