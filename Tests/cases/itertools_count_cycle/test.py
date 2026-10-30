from itertools import count, cycle, islice

print(list(islice(count(), 4)))
print(list(islice(count(10, -3), 4)))
print(list(islice(count(True), 3)))
print(list(islice(count(1.5, 0.5), 4)))
print(list(islice(cycle('abc'), 8)))
print(list(cycle([])))
try:
    count('x')
except TypeError as e:
    print(str(e))
