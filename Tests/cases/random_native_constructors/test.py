import _random
import gc

Random = _random.Random


def uninitialized(label, instance):
    state = instance.getstate()
    print(label, len(state), all(word == 0 for word in state),
          instance.random(), instance.getrandbits(80), instance.getstate()[-1])


uninitialized('explicit new', Random.__new__(Random))
uninitialized('new ignores arguments', Random.__new__(Random, 1, 2, seed=3))


class Child(Random):
    pass


uninitialized('subclass new', Random.__new__(Child, 5, ignored=True))
print('inherited init', Child(19).getstate() == Random(19).getstate())


class Override(Random):
    def __init__(self, *args, **kwargs):
        self.received = (args, kwargs)


instance = Override(1, 2, seed=3)
print('override arguments', instance.received)
uninitialized('override does not seed', instance)
print('explicit base init', Random.__init__(instance, 19, ignored=True),
      instance.getstate() == Random(19).getstate())


class Forward(Random):
    def __init__(self, seed, **kwargs):
        super().__init__(seed, **kwargs)


print('forwarded keywords', Forward(19, ignored=True).getstate() == Random(19).getstate())


class Alias(Random):
    __init__ = Random.__init__


def rejected(label, function, *args, **kwargs):
    try:
        function(*args, **kwargs)
    except Exception as exc:
        print(label, type(exc).__name__, str(exc))
    else:
        print(label, 'missing exception')


rejected('base keywords', Random, seed=19)
rejected('inherited keywords', Child, seed=19)
rejected('aliased keywords', Alias, seed=19)
rejected('base arity', Random, 1, 2)
rejected('keyword before arity', Random, 1, 2, seed=19)
rejected('explicit base keywords', Random.__init__, Random(19), seed=20)
rejected('explicit inherited keywords', Random.__init__, Child(19), seed=20)
before = instance.getstate()
rejected('override base arity', Random.__init__, instance, 1, 2, ignored=True)
print('rejection preserves state', instance.getstate() == before)


class Dynamic(Child):
    pass


instance = Dynamic(19)
Dynamic.__init__ = Override.__init__
print('changed initializer', Random.__init__(instance, 20, ignored=True),
      instance.getstate() == Random(20).getstate())
Dynamic.__init__ = Random.__init__
rejected('restored inherited initializer', Random.__init__, instance, 20, ignored=True)

events = []


class Seed:
    def __hash__(self):
        gc.collect()
        events.append('hash')
        return -17

    def __index__(self):
        raise AssertionError('seed must use hash, not index')


expected = Random((1 << 64) - 17).getstate()
print('constructor hash', Random(Seed()).getstate() == expected, events)
events.clear()
allocated = Random.__new__(Random, Seed())
print('new has no callbacks', events, allocated.getstate()[-1])
print('explicit init hash', Random.__init__(allocated, Seed()),
      allocated.getstate() == expected, events)
print('default init', Random.__init__(allocated), allocated.getstate()[0] == 1 << 31,
      allocated.getstate()[-1])


class BrokenSeed:
    def __hash__(self):
        gc.collect()
        events.append('broken hash')
        raise RuntimeError('seed failure')


before = allocated.getstate()
rejected('constructor failure', Random, BrokenSeed())
rejected('explicit init failure', Random.__init__, allocated, BrokenSeed())
print('failed conversion preserves state', allocated.getstate() == before)
print('failure callbacks', events)


class IntegerSeed(int):
    def __abs__(self):
        raise AssertionError('must not dispatch integer abs')

    def __hash__(self):
        raise AssertionError('must not hash integer seeds')


print('integer subclass', Random(IntegerSeed(-19)).getstate() == Random(19).getstate())
