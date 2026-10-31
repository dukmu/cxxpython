import socket
import socketserver

class Echo(socketserver.StreamRequestHandler):
    def handle(self):
        line = self.rfile.readline()
        self.wfile.write(line.upper())

server = socketserver.TCPServer(('127.0.0.1', 0), Echo)
server.timeout = 2
client = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
client.settimeout(2)
try:
    client.connect(server.server_address)
    client.sendall(b'hello server\n')
    server.handle_request()
    stream = client.makefile('rb')
    try:
        print(stream.readline())
        print(stream.read() == b'')
    finally:
        stream.close()
    print(server.fileno() == server.socket.fileno())
finally:
    client.close()
    server.server_close()
