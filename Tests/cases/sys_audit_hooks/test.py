import gc
import sys

print('no hooks', sys.audit(42))
events = []
blocked = [False]

def first(event, args):
    if event == 'sys.addaudithook' and blocked[0]:
        raise ValueError('veto registration')
    if event.startswith('cxx.audit'):
        gc.collect()
        events.append(('first', event, args))

def second(event, args):
    if event.startswith('cxx.audit'):
        events.append(('second', event, args))

sys.addaudithook(first)
sys.audit('cxx.audit.one', 1, 'a')
blocked[0] = True
print('veto', sys.addaudithook(second))
sys.audit('cxx.audit.two')
blocked[0] = False
sys.addaudithook(second)
sys.audit('cxx.audit.three', [3])
print('events', events)
try:
    sys.audit(42)
except TypeError as error:
    print('event type', str(error))
sys.audit('cxx.audit.nul\0ignored', 4)
print('nul', events[-2:])

def deny(event, args):
    if event == 'cxx.audit.denied':
        raise PermissionError('denied')

sys.addaudithook(deny)
try:
    sys.audit('cxx.audit.denied')
except PermissionError as error:
    print('propagated', str(error))
