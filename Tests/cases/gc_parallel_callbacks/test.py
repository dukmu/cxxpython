import _thread
import gc

gc.disable()
events = {}
errors = []
gates = [_thread.allocate_lock() for _ in range(3)]
for gate in gates:
    gate.acquire()

def observe(phase, info):
    ident = _thread.get_ident()
    events.setdefault(ident, []).append(phase)
    assert isinstance(info['collected'], int)
    assert info['uncollectable'] == 0
    assert any(item is events for item in gc.get_referents(observe.__globals__))
    if phase == 'start':
        gc.collect()

def worker(index):
    try:
        for i in range(10):
            cycle = [i]
            cycle.append(cycle)
            del cycle
            gc.collect()
    except BaseException as error:
        errors.append(type(error).__name__)
    finally:
        gates[index].release()

gc.callbacks.append(observe)
try:
    for i in range(len(gates)):
        _thread.start_new_thread(worker, (i,))
    for gate in gates:
        gate.acquire()
finally:
    gc.callbacks.clear()
print(errors)
print(sum(phases.count('start') for phases in events.values()))
print(all(phases == ['start', 'stop'] * (len(phases) // 2) for phases in events.values()))
gc.enable()
