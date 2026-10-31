import socket

server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
client = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
try:
    server.bind(('127.0.0.1', 0))
    server.listen(1)
    address = server.getsockname()
    print(address[0], address[1] > 0)
    client.connect(address)
    conn, peer = server.accept()
    try:
        print(peer[0], peer[1] > 0, client.getpeername() == address)
        client.sendall(b'tcp')
        print(conn.recv(3))
        conn.shutdown(socket.SHUT_WR)
        print(client.recv(1))
    finally:
        conn.close()
finally:
    client.close()
    server.close()

receiver = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sender = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
try:
    receiver.bind(('127.0.0.1', 0))
    address = receiver.getsockname()
    print(sender.sendto(bytearray(b'udp'), address))
    buf = bytearray(5)
    size, peer = receiver.recvfrom_into(buf)
    print(size, bytes(buf), peer[0], peer[1] > 0)
    receiver.sendto(b'ok', peer)
    print(sender.recvfrom(8)[0])
finally:
    sender.close()
    receiver.close()
