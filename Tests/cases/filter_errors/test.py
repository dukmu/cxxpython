def error(label, target, *args, **kwargs):
    try:
        target(*args, **kwargs)
    except Exception as exc:
        print(label, type(exc).__name__, str(exc))
    else:
        raise AssertionError(label)


for args in ((), (None,), (None, [], [])):
    error('arity', filter, *args)
error('keyword', filter, None, [], extra=True)
error('named positional', filter, function=None, iterable=[])
error('not iterable', filter, None, 42)


class BadIter:
    def __iter__(self):
        raise ValueError('iter failed')


error('iter', filter, None, BadIter())
print('noncallable empty', list(filter(42, [])))
value = filter(42, [1, 2])
error('noncallable first', next, value)
error('noncallable second', next, value)
print('noncallable consumed', next(value, 'done'))


def predicate(item):
    if item == 1:
        raise ValueError('predicate failed')
    if item == 3:
        raise StopIteration('predicate stop')
    return True


value = filter(predicate, range(1, 6))
error('predicate', next, value)
print('predicate retry', next(value))
error('predicate stop', next, value)
print('predicate stop retry', list(value))


class Flag:
    def __init__(self, index):
        self.index = index

    def __bool__(self):
        if self.index == 1:
            raise ValueError('truth failed')
        if self.index == 3:
            raise StopIteration('truth stop')
        return True


value = filter(Flag, range(1, 6))
error('truth', next, value)
print('truth retry', next(value))
error('truth stop', next, value)
print('truth stop retry', list(value))
value = filter(None, [Flag(1), Flag(2)])
error('direct truth', next, value)
print('direct truth retry', next(value).index)


class Resuming:
    def __init__(self):
        self.index = 0

    def __iter__(self):
        return self

    def __next__(self):
        self.index += 1
        if self.index == 1:
            raise ValueError('next failed')
        if self.index == 2:
            raise StopIteration('child stop')
        return self.index


value = filter(None, Resuming())
error('child', next, value)
error('child stop', next, value)
print('child stop retry', next(value))


class BadBool:
    def __bool__(self):
        return 1


class BadLen:
    def __len__(self):
        return -1


# Shared truth-protocol wording is tested by its owner, not redefined by filter.
for item in (BadBool(), BadLen()):
    value = filter(None, [item, 9])
    try:
        next(value)
    except (TypeError, ValueError) as exc:
        print('truth protocol', type(exc).__name__, next(value))
    else:
        raise AssertionError('invalid truth value accepted')


def reenter(item):
    if item == 1:
        print('reentrant child', next(value))
    return True


value = filter(reenter, [1, 2, 3])
print('reentrant outer', next(value), list(value))
