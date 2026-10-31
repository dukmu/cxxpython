import socket
import socketserver
import threading

class Echo(socketserver.StreamRequestHandler):
    def handle(self):
        self.wfile.write(self.rfile.readline().upper())

server = socketserver.ThreadingTCPServer(('127.0.0.1', 0), Echo)
server.daemon_threads = True
thread = threading.Thread(target=server.serve_forever, args=(0.01,), daemon=True)
client = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
client.settimeout(2)
try:
    thread.start()
    client.connect(server.server_address)
    client.sendall(b'threaded\n')
    stream = client.makefile('rb')
    try:
        print(stream.readline())
    finally:
        stream.close()
    stopper = threading.Thread(target=server.shutdown, daemon=True)
    stopper.start()
    stopper.join(2)
    thread.join(2)
    print(not stopper.is_alive(), not thread.is_alive())
finally:
    client.close()
    server.server_close()
