import time

print(type(time))
print(time.__doc__.__contains__('Time access helpers'))
print(time.__all__.__contains__('time'))
print(time.__all__.__contains__('sleep'))
print(time.time() > 0)
print(time.monotonic() >= 0)
