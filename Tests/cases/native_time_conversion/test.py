import gc
import select
import socket
import time

class Zero:
    def __index__(self):
        gc.collect()
        return 0

class Broken:
    def __index__(self):
        raise TypeError('conversion callback failed')

operations = (('sleep', time.sleep),
              ('select', lambda value: select.select([], [], [], value)),
              ('socket', socket.setdefaulttimeout))
previous = socket.getdefaulttimeout()
try:
    for label, operation in operations:
        print(label, operation(Zero()))
        for value in (-1, -0.0000000001, float('nan'), float('inf'), 1 << 100):
            try:
                operation(value)
            except Exception as error:
                print(label, type(error).__name__, str(error))
        try:
            operation(Broken())
        except TypeError as error:
            print(label, str(error))
    socket.setdefaulttimeout(0.0000000001)
    print(socket.getdefaulttimeout())
    print(time.sleep(0.0000000001))
    print(select.select([], [], [], 0.0000000001))
finally:
    socket.setdefaulttimeout(previous)
