import _thread

events = []


class Manager:
    def __init__(self, name, suppress=False):
        self.name = name
        self.suppress = suppress

    def __enter__(self):
        events.append('enter ' + self.name)
        return 'value ' + self.name

    def __exit__(self, exc_type, exc, tb):
        events.append('exit ' + self.name + ' ' + str(exc_type is None))
        if exc_type is ValueError:
            events.append(str(exc))
            events.append(str(tb is None))
        return self.suppress


with Manager('a') as value:
    events.append(value)

try:
    with Manager('boom'):
        raise ValueError('bad')
except ValueError as exc:
    events.append('caught ' + str(exc))

with Manager('suppress', True):
    raise ValueError('hidden')
events.append('after suppress')

with Manager('outer') as outer, Manager('inner') as inner:
    events.append(outer + ' / ' + inner)

lock = _thread.allocate_lock()
with lock:
    print(lock.locked())
print(lock.locked())

for event in events:
    print(event)
