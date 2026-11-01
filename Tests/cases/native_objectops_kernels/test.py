import _thread
import gc
import operator


binary = (
    ('add', operator.add, operator.iadd),
    ('sub', operator.sub, operator.isub),
    ('mul', operator.mul, operator.imul),
    ('truediv', operator.truediv, operator.itruediv),
    ('floordiv', operator.floordiv, operator.ifloordiv),
    ('mod', operator.mod, operator.imod),
    ('pow', operator.pow, operator.ipow),
    ('lshift', operator.lshift, operator.ilshift),
    ('rshift', operator.rshift, operator.irshift),
    ('and', operator.and_, operator.iand),
    ('or', operator.or_, operator.ior),
    ('xor', operator.xor, operator.ixor),
)


def outcome(function, left, right):
    try:
        return repr(function(left, right))
    except (ArithmeticError, ValueError, TypeError) as error:
        return type(error).__name__


for name, normal, inplace in binary:
    for left, right in ((-7, 3), (7, -3), ((1 << 63) - 1, 2),
                        (-(1 << 63), 2), (1 << 100, 3), (True, False)):
        # Keep exponentiation and shifts bounded while still exercising big operands.
        if name in ('pow', 'lshift', 'rshift') and abs(right) > 10:
            continue
        print(name, repr(left), repr(right), outcome(normal, left, right),
              outcome(inplace, left, right))
    print(name, 'zero', outcome(normal, 1, 0), outcome(inplace, 1, 0))

for name, normal, inplace in binary:
    for left, right in ((-7.5, 2.0), (7.5, -2.0), (1.0, 0.0),
                        (-0.0, 2.0), (float('inf'), 2.0),
                        (float('nan'), 2.0), (3, 2.5), (3.5, 2)):
        print('float', name, repr(left), repr(right), outcome(normal, left, right),
              outcome(inplace, left, right))


class Number(int):
    def __add__(self, other):
        gc.collect()
        return 'add'

    def __radd__(self, other):
        gc.collect()
        return 'radd'

    def __iadd__(self, other):
        gc.collect()
        return 'iadd'


class Child(Number):
    def __radd__(self, other):
        gc.collect()
        return 'child-radd'


print('dispatch', Number(1) + 2, 2 + Number(1), Number(1) + Child(2))
value = Number(1)
value += 2
print('inplace', value)


class PlainInt(int):
    pass


class PlainFloat(float):
    pass


print('subclass fallback', PlainInt(3) * 2, 2 + PlainInt(3),
      PlainFloat(1.5) + 2, True + PlainInt(3))


class Left:
    def __add__(self, other):
        gc.collect()
        type(other).__radd__ = replacement
        return NotImplemented


def replacement(self, other):
    gc.collect()
    return 'replacement'


class Right:
    def __radd__(self, other):
        return 'original'


print('replace during dispatch', Left() + Right())
del Right.__radd__
print('deleted dispatch', outcome(operator.add, Left(), Right()))
print('installed dispatch', Left() + Right())


class ForwardOnly:
    def __add__(self, other):
        return NotImplemented


print('existing forward slot', Left() + ForwardOnly())


class DeleteLeft:
    def __add__(self, other):
        gc.collect()
        del DeleteRight.__radd__
        return NotImplemented


class DeleteRight:
    def __radd__(self, other):
        return 'removed'


print('delete during dispatch', outcome(operator.add, DeleteLeft(), DeleteRight()))


class Base:
    def __add__(self, other):
        return 'base-add'

    def __radd__(self, other):
        return 'base-radd'


class Derived(Base):
    def __radd__(self, other):
        gc.collect()
        return 'derived-radd'


print('precedence', Base() + Derived())
del Derived.__radd__
print('inherited precedence', Base() + Derived())
Derived.__radd__ = replacement
print('replaced precedence', Base() + Derived())


class Index:
    def __init__(self, value, action=None):
        self.value = value
        self.action = action

    def __index__(self):
        gc.collect()
        if self.action is not None:
            self.action()
        return self.value


for sequence in ([0, 1, 2, 3], (0, 1, 2, 3), 'abcd'):
    print('read', sequence[Index(-1)], sequence[Index(1):Index(4):Index(2)])
    for index in (99, 1 << 100, 'bad', slice(None, None, 0)):
        try:
            sequence[index]
        except (IndexError, TypeError, ValueError) as error:
            print('read error', type(error).__name__)

items = [10, 20, 30]
items[Index(1, lambda: items.insert(0, 5))] = 99
del items[Index(-1, lambda: items.append(40))]
items[Index(1):Index(3)] = [7, 8, 9]
del items[Index(0):Index(2)]
print('mutated', items)
for action in (lambda: items.__setitem__(99, 1),
               lambda: items.__delitem__(99),
               lambda: items.__setitem__(slice(None, None, 2), [1])):
    try:
        action()
    except (IndexError, ValueError) as error:
        print('write error', type(error).__name__)

items = [['kept'], ['removed']]
result = items[Index(0, lambda: items.pop())]
items.clear()
gc.collect()
print('rooted result', result)

lock = _thread.allocate_lock()
done = [_thread.allocate_lock(), _thread.allocate_lock()]
for event in done:
    event.acquire()
shared = []
results = [None, None]


def worker(slot):
    total = 0
    for i in range(100):
        total += (i * 3) // 2
        with lock:
            shared.append((slot, i))
            last = shared[-1]
            assert last == (slot, i)
        if i % 20 == 0:
            gc.collect()
    results[slot] = total
    done[slot].release()


for slot in range(2):
    _thread.start_new_thread(worker, (slot,))
for event in done:
    event.acquire()
print('threads', results, len(shared), sorted(shared) ==
      [(slot, i) for slot in range(2) for i in range(100)])
