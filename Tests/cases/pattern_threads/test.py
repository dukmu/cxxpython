import _thread
import gc

subject = {'items': [1, 2, 3], 'label': 'shared'}
results = [None, None]
done = [_thread.allocate_lock(), _thread.allocate_lock()]
for lock in done:
    lock.acquire()


def worker(index):
    try:
        total = 0
        for i in range(100):
            match subject:
                case {'items': [a, *middle, c], 'label': label} if label == 'shared':
                    total += a + middle[0] + c
            if i % 10 == 0:
                gc.collect()
        results[index] = total
    except BaseException as exc:
        results[index] = type(exc).__name__
    finally:
        done[index].release()


for i in range(2):
    _thread.start_new_thread(worker, (i,))
for lock in done:
    lock.acquire()
print('shared', results)


def gen():
    match [5, 6]:
        case [a, b] if (yield a):
            yield b


class Pause:
    def __await__(self):
        yield 'paused'
        return True


async def coro():
    match {'value': 8}:
        case {'value': value} if await Pause():
            return value


g = gen()
c = coro()
print('initial', next(g), c.send(None))
handoff = _thread.allocate_lock()
handoff.acquire()
result = [None]


def resume():
    try:
        item = g.send(True)
        try:
            c.send(None)
        except StopIteration as exc:
            result[0] = (item, exc.value)
    except BaseException as exc:
        result[0] = type(exc).__name__
    finally:
        handoff.release()


_thread.start_new_thread(resume, ())
handoff.acquire()
print('handoff', result[0])

# ABC registration while class construction is suspended reaches the child.
from collections.abc import Sequence

creating = _thread.allocate_lock()
registered = _thread.allocate_lock()
complete = _thread.allocate_lock()
for lock in (creating, registered, complete):
    lock.acquire()
constructed = []


class Parent:
    pass


class PausedDescriptor:
    def __set_name__(self, owner, name):
        creating.release()
        registered.acquire()


def construct():
    try:
        class Child(Parent):
            pause = PausedDescriptor()
        constructed.append(Child)
    except BaseException as exc:
        constructed.append(type(exc).__name__)
    finally:
        complete.release()


_thread.start_new_thread(construct, ())
creating.acquire()
Sequence.register(Parent)
registered.release()
complete.acquire()
match constructed[0]():
    case [*_]:
        print('registration during construction', True)
    case _:
        print('registration during construction', False)
