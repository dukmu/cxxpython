from contextlib import ExitStack, contextmanager

events = []

@contextmanager
def resource(name):
    events.append('enter ' + name)
    try:
        yield name
    finally:
        events.append('exit ' + name)

with ExitStack() as stack:
    print('entered', stack.enter_context(resource('a')))
    stack.callback(events.append, 'callback')
    stack.enter_context(resource('b'))
    moved = stack.pop_all()
print('before close', events)
moved.close()
print('after close', events)

def observe(typ, value, tb):
    events.append(typ.__name__ if typ else 'none')
    return False

def suppress(typ, value, tb):
    events.append('suppress ' + typ.__name__)
    return True

with ExitStack() as stack:
    stack.push(observe)
    stack.push(suppress)
    raise ValueError('body')
print('suppression', events[-2:])

try:
    ExitStack().enter_context(object())
except TypeError as exc:
    print('invalid context', type(exc).__name__)
