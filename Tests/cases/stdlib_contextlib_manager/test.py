from contextlib import contextmanager, ContextDecorator

events = []

@contextmanager
def managed(value):
    events.append('enter')
    try:
        yield value
    except ValueError:
        events.append('caught')
    finally:
        events.append('exit')

with managed(42) as value:
    print('value', value)
    raise ValueError('suppressed')
print('events', events)

@managed('decorated')
def decorated(value):
    events.append(value)
    return value * 2

print('decorator', decorated(3), decorated(4), events)

class Decorator(ContextDecorator):
    def __enter__(self):
        events.append('decorator enter')
    def __exit__(self, typ, value, tb):
        events.append(typ.__name__ if typ else 'decorator exit')
        return False

@Decorator()
def fail():
    raise LookupError('failure')

try:
    fail()
except LookupError as exc:
    print('propagated', str(exc), events[-2:])

@contextmanager
def empty():
    if False:
        yield

@contextmanager
def twice():
    yield 1
    yield 2

for factory in (empty, twice):
    try:
        with factory():
            pass
    except RuntimeError as exc:
        print('invalid generator', str(exc))
