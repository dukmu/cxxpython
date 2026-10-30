from _collections import deque

d = deque([1, 2, 3], 3)
d.append(4)
print(list(d), d.maxlen)
d.appendleft(0)
print(list(d))
d.rotate(1)
print(list(d), d[0], d[-1])
d[1] = 8
del d[-1]
d.extend([4, 5])
d.extendleft([6, 7])
print(list(d), d.count(6), d.index(6))
d.reverse()
print(list(d), d.pop(), d.popleft())
d = deque([1, 2, 3])
d.insert(1, 9)
d.remove(2)
print(repr(d), d == deque([1, 9, 3]), d < deque([2]))
it = iter(d)
print(next(it))
d.append(4)
try:
    next(it)
except RuntimeError as exc:
    print(type(exc).__name__, str(exc))
it = reversed(d)
print(next(it))
d.popleft()
try:
    next(it)
except RuntimeError as exc:
    print(type(exc).__name__, str(exc))
try:
    deque([1], 1).insert(0, 2)
except IndexError as exc:
    print(type(exc).__name__, str(exc))
