import gc
import math

events = []

class Stream:
    def __init__(self, name, values):
        self.name = name
        self.values = values
        self.position = 0
    def __iter__(self):
        events.append(self.name + '.iter')
        gc.collect()
        return self
    def __next__(self):
        events.append(self.name + '.next')
        gc.collect()
        if self.position == len(self.values):
            raise StopIteration
        value = self.values[self.position]
        self.position += 1
        return value

class Number:
    def __init__(self, name, value):
        self.name = name
        self.value = value
    def __float__(self):
        events.append(self.name + '.float')
        gc.collect()
        return self.value

print(math.fsum(Stream('s', [Number('a', 1.5), Number('b', 2.5)])))
print(events)
events.clear()
print(math.hypot(Number('a', 3.0), Number('b', 4.0)))
print(events)
events.clear()
print(math.dist(Stream('p', [Number('px', 3.0), Number('py', 4.0)]),
                Stream('q', [Number('qx', 0.0), Number('qy', 0.0)])))
print(events)
events.clear()
print(math.sumprod(Stream('p', [2, 3]), Stream('q', [4, 5])))
print(events)
events.clear()
try:
    math.sumprod(Stream('p', []), Stream('q', [1]))
except ValueError as error:
    print(str(error))
print(events)
events.clear()

class Fail:
    def __float__(self):
        events.append('fail.float')
        raise LookupError('conversion')

for function, args in [(math.fsum, ([Number('before', 1.0), Fail(), Number('after', 2.0)],)),
                       (math.dist, ([Fail()], []))]:
    try:
        function(*args)
    except Exception as error:
        print(type(error).__name__, str(error))
print(events)

def broken():
    yield 1
    raise RuntimeError('iteration')

for function, args in [(math.fsum, (broken(),)), (math.prod, (broken(),)),
                       (math.sumprod, (broken(), [1, 2])),
                       (math.dist, (broken(), [1, 2]))]:
    try:
        function(*args)
    except RuntimeError as error:
        print(str(error))
