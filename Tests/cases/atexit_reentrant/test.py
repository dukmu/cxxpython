import atexit
import gc
import weakref

atexit._clear()
events = []

def older():
    events.append('older')

def newer():
    events.append('newer')
    atexit.unregister(newer)
    atexit._run_exitfuncs()
    events.append('returned')

atexit.register(older)
atexit.register(newer)
atexit._run_exitfuncs()
print(events, atexit._ncallbacks())

class Item:
    def __call__(self, *args, **kwargs):
        pass

function = Item()
argument = Item()
function_ref = weakref.ref(function)
argument_ref = weakref.ref(argument)
atexit.register(function, argument, value=argument)
atexit.unregister(function)
del function, argument
gc.collect()
print(function_ref() is None, argument_ref() is None, atexit._ncallbacks())
atexit._clear()
