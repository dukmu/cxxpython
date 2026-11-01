import gc
import math
import _weakref


events = []
holder = []
last = []
finalized = []


class Number:
    def __init__(self, value):
        self.value = value

    def __float__(self):
        events.append(('float', self.value))
        holder.clear()
        gc.collect()
        return float(self.value)

    def __del__(self):
        finalized.append(self.value)


class After:
    def __iter__(self):
        return self

    def __next__(self):
        gc.collect()
        events.append(('after', last[0]() is None))
        raise StopIteration


class Before:
    def __iter__(self):
        return self

    def __next__(self):
        events.append('before')
        self.__class__ = After
        value = Number(2)
        last.append(_weakref.ref(value))
        return value


holder.append(Before())
print('fsum', math.fsum(holder[0]))
gc.collect()
print('fsum events', events, 'dead', last[0]() is None, 'finalized', finalized)


class Changing:
    def __iter__(self):
        return self

    def __next__(self):
        events.append('first')
        Changing.__next__ = second
        holder.clear()
        gc.collect()
        return 1


def second(self):
    events.append('second')
    Changing.__next__ = finish
    gc.collect()
    return 2


def finish(self):
    events.append('finish')
    gc.collect()
    raise StopIteration


first_method = Changing.__next__
for name, consume in [('sum', sum), ('min', min), ('max', max),
                      ('all', all), ('any', any), ('list', list), ('tuple', tuple)]:
    events.clear()
    Changing.__next__ = first_method
    holder.append(Changing())
    print(name, consume(holder[0]), events)


class Retained:
    def __init__(self):
        self.first = True

    def __iter__(self):
        return self

    def __next__(self):
        if self.first:
            self.first = False
            value = Number(9)
            last.append(_weakref.ref(value))
            return value
        gc.collect()
        events.append(last[-1]() is not None)
        raise StopIteration


for name, consume in [('list', list), ('tuple', tuple)]:
    events.clear()
    finalized.clear()
    result = consume(Retained())
    gc.collect()
    print(name, 'retained', events, last[-1]() is result[0])
    del result
    gc.collect()
    print(name, 'released', last[-1]() is None, finalized)


class Broken:
    def __iter__(self):
        return self

    def __next__(self):
        gc.collect()
        raise ValueError('iterator failure')


for name, consume in [('fsum', math.fsum), ('sum', sum), ('min', min), ('max', max),
                      ('all', all), ('any', any), ('list', list), ('tuple', tuple)]:
    try:
        consume(Broken())
    except ValueError as exc:
        print(name, type(exc).__name__, str(exc))


ended = []


class EndWithValue:
    def __iter__(self):
        return self

    def __next__(self):
        value = Number(7)
        ended.append(_weakref.ref(value))
        raise StopIteration(value)


class ProbeEnd:
    def __iter__(self):
        return self

    def __next__(self):
        gc.collect()
        print('terminal released', ended[0]() is None)
        raise StopIteration


print('empty sumprod', math.sumprod(EndWithValue(), ProbeEnd()))

