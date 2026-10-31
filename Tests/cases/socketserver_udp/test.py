import socket
import socketserver

class Echo(socketserver.DatagramRequestHandler):
    def handle(self):
        self.wfile.write(self.rfile.read().upper())

server = socketserver.UDPServer(('127.0.0.1', 0), Echo)
server.timeout = 2
client = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
client.settimeout(2)
try:
    client.sendto(b'datagram', server.server_address)
    server.handle_request()
    data, address = client.recvfrom(64)
    print(data, address == server.server_address)
finally:
    client.close()
    server.server_close()
