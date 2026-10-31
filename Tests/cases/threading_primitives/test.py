import threading

# CPython 3.12.14 test_threading exercises Condition, Event, Semaphore and
# Barrier independently; this also checks the Timer cancellation contract.
errors = []
condition = threading.Condition()
ready = threading.Event()
state = []


def waiter():
    try:
        with condition:
            ready.set()
            assert condition.wait_for(lambda: bool(state), timeout=5)
            state.append("woken")
    except BaseException as exc:
        errors.append(type(exc).__name__)
        ready.set()


t = threading.Thread(target=waiter)
t.start()
assert ready.wait(5)
with condition:
    state.append("notified")
    condition.notify()
t.join(5)
assert not t.is_alive() and not errors
print("condition", state)
event = threading.Event()
print("event", event.wait(0), event.is_set())
event.set()
print("event set", event.wait(0))
event.clear()
print("event clear", event.is_set())
sem = threading.BoundedSemaphore(1)
print("semaphore", sem.acquire(False), sem.acquire(False))
sem.release()
try:
    sem.release()
except ValueError:
    print("over-release rejected")

barrier = threading.Barrier(2)
indices = []
def meet():
    try:
        indices.append(barrier.wait(5))
    except BaseException as exc:
        errors.append(type(exc).__name__)

t = threading.Thread(target=meet)
t.start()
indices.append(barrier.wait(5))
t.join(5)
assert not t.is_alive() and not errors
print("barrier", sorted(indices), barrier.broken)
timer_fired = threading.Event()
timer = threading.Timer(5, timer_fired.set)
timer.start()
timer.cancel()
timer.join(5)
assert not timer.is_alive()
print("timer cancelled", timer_fired.is_set())
timer = threading.Timer(0.001, timer_fired.set)
timer.start()
timer.join(5)
assert not timer.is_alive()
print("timer fired", timer_fired.is_set())
