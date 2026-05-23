import threading


print(type(threading))
print(threading.__doc__.__contains__('Thread objects'))
print(threading.__all__.__contains__('Thread'))
print(threading.__all__.__contains__('Lock'))
print(type(threading.Lock()))
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
