import atexit
import sys

atexit._clear()
events = []

def record(label, *, value):
    events.append(label + ':' + value)

print(atexit.register(record, 'first', value='one') is record)
atexit.register(record, 'second', value='two')
print(atexit._ncallbacks())
atexit._run_exitfuncs()
print(events)
print(atexit._ncallbacks())

class Callback:
    def __init__(self, label):
        self.label = label
    def __eq__(self, other):
        return self.label == other.label
    def __call__(self):
        events.append(self.label)

atexit.register(Callback('drop'))
atexit.register(Callback('keep'))
atexit.unregister(Callback('drop'))
print(atexit._ncallbacks())
atexit._run_exitfuncs()
print(events)

def hook(info):
    print('hook', type(info.exc_value).__name__, info.err_msg, info.object.__name__)

def bad():
    raise ValueError('failure')

old = sys.unraisablehook
sys.unraisablehook = hook
atexit.register(record, 'after', value='error')
atexit.register(bad)
atexit._run_exitfuncs()
sys.unraisablehook = old
print(events)
try:
    atexit.register(1)
except TypeError:
    print('not callable')
atexit.register(record, 'discard', value='yes')
atexit._clear()
print(atexit._ncallbacks())
