import gc
import sys

saved = sys.stdout
events = []


class Stream:
    def write(self, text):
        gc.collect()
        events.append(text)

    def flush(self):
        gc.collect()
        events.append('flush')


class Item:
    def __str__(self):
        gc.collect()
        events.append('str')
        return 'item'


class Bad:
    def __str__(self):
        events.append('bad')
        raise ValueError('bad')


sys.stdout = Stream()
print(Item(), Item(), sep=None, end=None, flush=True)
assert events == ['str', 'item', ' ', 'str', 'item', '\n', 'flush']
events = []
try:
    print(Item(), Bad(), flush=True)
except ValueError:
    pass
assert events == ['str', 'item', ' ', 'bad']
events = []
try:
    print(Item(), sep=42)
except TypeError:
    pass
assert events == []


class FailingStream:
    def write(self, text):
        events.append('write')
        raise ValueError('write')

    def flush(self):
        events.append('flush')


events = []
try:
    print(Item(), Item(), file=FailingStream(), flush=True)
except ValueError:
    pass
assert events == ['str', 'write']


class FailingFlush:
    def write(self, text):
        events.append(text)

    def flush(self):
        events.append('flush')
        raise ValueError('flush')


events = []
try:
    print('x', file=FailingFlush(), flush=True)
except ValueError:
    pass
assert events == ['x', '\n', 'flush']
sys.stdout = None
print(Bad())
print(Bad(), sep=42)
sys.stdout = saved
print('print callbacks ok')
