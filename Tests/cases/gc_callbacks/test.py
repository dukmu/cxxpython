import gc
import sys

gc.disable()
events = []
errors = []

def first(phase, info):
    events.append(('first', phase, sorted(info)))
    assert isinstance(info['collected'], int)
    assert info['uncollectable'] == 0
    if phase == 'start':
        gc.collect()

def second(phase, info):
    events.append(('second', phase, info['collected'] >= 0))

def fail(phase, info):
    raise ValueError(phase)

def unraisable(event):
    errors.append((event.exc_type.__name__, str(event.exc_value), event.object is fail))

original = sys.unraisablehook
sys.unraisablehook = unraisable
gc.callbacks[:] = [first, fail, second]
try:
    gc.collect()
finally:
    gc.callbacks.clear()
    sys.unraisablehook = original
print(events)
print(errors)

events.clear()
def appended(phase, info):
    events.append(('appended', phase))

def mutate(phase, info):
    events.append(('mutate', phase))
    if phase == 'start':
        gc.callbacks.append(appended)

gc.callbacks.append(mutate)
gc.collect()
gc.callbacks.clear()
print(events)
gc.enable()
