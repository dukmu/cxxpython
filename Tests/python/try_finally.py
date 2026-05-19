events = []
try:
    events.append('body')
finally:
    events.append('finally')
print(events)

try:
    try:
        raise TypeError('boom')
    finally:
        events.append('cleanup')
except TypeError as e:
    print(e)
print(events)

events = []
try:
    raise TypeError('handled')
except TypeError:
    events.append('except')
finally:
    events.append('finally')
print(events)

events = []
def f():
    try:
        return 'body'
    finally:
        events.append('finally')

print(f())
print(events)

try:
    try:
        raise TypeError('body')
    finally:
        raise ValueError('finally')
except ValueError as e:
    print(e)
