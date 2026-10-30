import gc
import sys
import _thread

gc.disable()
assert gc.is_tracked(123) and gc.is_tracked('leaf')
assert gc.get_objects(0) == [] and gc.get_objects(1) == []
stats = gc.get_stats()
assert stats[0] == stats[1] == {'collections': 0, 'collected': 0, 'uncollectable': 0}
assert stats[2]['collections'] > 0
assert all(isinstance(type(obj).__name__, str) for obj in gc.get_objects())

before = gc.get_heap_stats()
saved = [[i] for i in range(500)]
allocated = gc.get_heap_stats()
assert allocated['allocations_since_gc'] > before['allocations_since_gc']
assert allocated['objects'] > before['objects']
assert allocated['allocated_bytes'] > before['allocated_bytes']
assert allocated['reserved_bytes'] > 0
assert allocated['registered_threads'] == 1
del saved
gc.collect()
after = gc.get_heap_stats()
assert after['reclaimed_objects'] > before['reclaimed_objects']
assert after['collections'] > before['collections']
assert after['last_pause_ns'] > 0
assert after['gc_budget'] > 0

events = []
original = gc.callbacks
def observed(phase, info):
    assert info['generation'] == 2
    assert isinstance(gc.get_objects(), list)
    events.append(phase)

original.append(observed)
gc.callbacks = []
gc.collect()
assert events == ['start', 'stop']
gc.callbacks = original
original.clear()
saved_module = sys.modules.pop('gc')
import gc as reimported
assert reimported.callbacks is original
sys.modules['gc'] = saved_module

# A callback may park on user I/O/locks without preventing another owner collecting.
main_id = _thread.get_ident()
entered = _thread.allocate_lock()
resume = _thread.allocate_lock()
finished = _thread.allocate_lock()
for gate in (entered, resume, finished):
    gate.acquire()
progress = []
errors = []
def parking_callback(phase, info):
    if phase == 'start' and _thread.get_ident() == main_id:
        entered.release()
        resume.acquire()
    elif phase == 'stop' and _thread.get_ident() != main_id:
        progress.append('worker callback')

def collector():
    try:
        entered.acquire()
        gc.collect()
        progress.append('worker collection')
    except BaseException as error:
        errors.append(type(error).__name__)
    finally:
        resume.release()
        finished.release()

original.append(parking_callback)
_thread.start_new_thread(collector, ())
try:
    gc.collect()
    finished.acquire()
finally:
    original.clear()
assert errors == []
assert progress == ['worker callback', 'worker collection']

events.clear()
def automatic(phase, info):
    events.append(phase)

gc.callbacks.append(automatic)
gc.enable()
saved = bytearray(gc.get_heap_stats()['gc_budget'])
probe = []
gc.disable()
gc.callbacks.clear()
assert events and len(events) % 2 == 0
assert events == ['start', 'stop'] * (len(events) // 2)
