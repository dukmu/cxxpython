import selectors
import socket

left, right = socket.socketpair()
selector = selectors.SelectSelector()
try:
    key = selector.register(left, selectors.EVENT_READ, 'first')
    print(key.fileobj is left, key.fd == left.fileno(), key.data)
    print(len(selector.get_map()), selector.get_key(left) == key)
    try:
        selector.register(left, selectors.EVENT_READ)
    except KeyError:
        print('duplicate')
    try:
        selector.modify(left, 0)
    except ValueError:
        print('invalid events', len(selector.get_map()))
    selector.register(left, selectors.EVENT_READ, 'first')
    key = selector.modify(left, selectors.EVENT_READ | selectors.EVENT_WRITE, 'second')
    events = selector.select(0)
    print(key.data, len(events), events[0][0] == key,
          events[0][1] == selectors.EVENT_WRITE)
    right.sendall(b'x')
    events = selector.select(0)
    print(events[0][1] == selectors.EVENT_READ | selectors.EVENT_WRITE)
    print(left.recv(1), selector.unregister(left) == key, len(selector.get_map()))
    try:
        selector.get_key(left)
    except KeyError:
        print('unregistered')
finally:
    selector.close()
    left.close()
    right.close()
print(selector.get_map() is None)
