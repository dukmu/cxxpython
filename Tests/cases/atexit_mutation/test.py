import atexit
import gc

atexit._clear()
events = []

class Callback:
    def __init__(self, name):
        self.name = name
    def __call__(self):
        events.append(self.name)
    def __eq__(self, other):
        gc.collect()
        return self.name == other.name

atexit.register(Callback('remove'))
atexit.register(Callback('keep'))
atexit.unregister(Callback('remove'))
gc.collect()
atexit._run_exitfuncs()
print(events)

def late():
    events.append('late')

def add_late():
    events.append('add')
    atexit.register(late)

atexit.register(add_late)
atexit._run_exitfuncs()
atexit._run_exitfuncs()
print(events, atexit._ncallbacks())
