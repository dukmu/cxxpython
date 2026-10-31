import select
import signal
import socket
import threading

left, right = socket.socketpair()
seen = []
ready = threading.Event()

def handler(number, frame):
    seen.append(number)

def worker():
    ready.wait()
    signal.raise_signal(signal.SIGUSR1)
    right.sendall(b'x')

previous = signal.signal(signal.SIGUSR1, handler)
thread = threading.Thread(target=worker, daemon=True)
try:
    thread.start()
    ready.set()
    reads, _, _ = select.select([left], [], [], 2)
    thread.join(2)
    print(reads == [left], left.recv(1) if reads else None)
    print(seen == [signal.SIGUSR1], not thread.is_alive())
finally:
    signal.signal(signal.SIGUSR1, previous)
    left.close()
    right.close()
