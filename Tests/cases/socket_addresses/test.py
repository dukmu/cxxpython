import socket

print(socket.inet_aton('127.0.0.1'))
print(socket.inet_ntoa(memoryview(b'\x7f\0\0\1')))
for family, text in ((socket.AF_INET, '192.0.2.1'), (socket.AF_INET6, '2001:db8::1234')):
    packed = socket.inet_pton(family, text)
    print(len(packed), socket.inet_ntop(family, packed))
print(socket.ntohs(socket.htons(12345)), socket.ntohl(socket.htonl(0x12345678)))
print(isinstance(socket.gethostname(), str))
info = socket.getaddrinfo('127.0.0.1', 80, socket.AF_INET,
                          socket.SOCK_STREAM, socket.IPPROTO_TCP,
                          socket.AI_NUMERICHOST | socket.AI_NUMERICSERV)
print(info[0], len(info))
print(socket.getnameinfo(('127.0.0.1', 80), socket.NI_NUMERICHOST | socket.NI_NUMERICSERV))
print(socket.gethostbyname('127.0.0.1'))
for operation in (lambda: socket.inet_aton('not an IP'),
                  lambda: socket.inet_ntoa(b'123'),
                  lambda: socket.inet_pton(socket.AF_INET, '300.0.0.1'),
                  lambda: socket.inet_ntop(socket.AF_INET, b'123'),
                  lambda: socket.getaddrinfo('not-an-ip', 80, flags=socket.AI_NUMERICHOST),
                  lambda: socket.getnameinfo(('localhost', 80), 0),
                  lambda: socket.getaddrinfo('127.0.0.1', 80, 1 << 40),
                  lambda: socket.inet_pton(1 << 40, '127.0.0.1')):
    try:
        operation()
    except Exception as exc:
        print(type(exc).__name__)

server = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
client = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
try:
    server.bind('unix-server.sock')
    client.bind('unix-client.sock')
    print(server.getsockname(), client.getsockname())
    client.sendto(b'unix', 'unix-server.sock')
    data, peer = server.recvfrom(8)
    print(data, peer)
finally:
    server.close()
    client.close()
