import threading


print(type(threading))
print(threading.__doc__.__contains__('Thread objects'))
print(threading.__all__.__contains__('Thread'))
print(threading.__all__.__contains__('Lock'))
print(threading.__all__.__contains__('RLock'))
print(threading.__all__.__contains__('Condition'))
print(threading.__all__.__contains__('Event'))
print(type(threading.Lock()))
print(type(threading.RLock()))
print(type(threading.get_ident()))
print(threading.current_thread().name)
print(type(threading.current_thread().ident))

values = []


def target(prefix, suffix=None):
    values.append(prefix + suffix)


thread = threading.Thread(target=target, args=('worker',), kwargs={'suffix': '-done'})
print(thread.is_alive())
thread.start()
print(type(thread.ident))
thread.join()
print(thread.is_alive())
print(values)

try:
    thread.start()
except ValueError as exc:
    print(type(exc))

rlock = threading.RLock()
with rlock:
    with rlock:
        values.append('rlock')
print(rlock.locked())

condition = threading.Condition()
ready = threading.Event()
events = []


def waiter():
    with condition:
        ready.set()
        condition.wait()
        events.append('notified')


wait_thread = threading.Thread(target=waiter)
wait_thread.start()
ready.wait()
with condition:
    events.append('notify')
    condition.notify()
wait_thread.join()
print(events)

event = threading.Event()
print(event.is_set())


def set_event():
    event.set()


event_thread = threading.Thread(target=set_event)
event_thread.start()
print(event.wait())
event_thread.join()
print(event.is_set())
