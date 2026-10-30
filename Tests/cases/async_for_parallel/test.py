import _thread
import gc
import sys


class Pause:
    def __init__(self, value):
        self.value = value

    def __await__(self):
        gc.collect()
        yield self.value


class Items:
    def __init__(self):
        self.value = 0

    def __aiter__(self):
        return self

    async def __anext__(self):
        self.value += 1
        if self.value > 2:
            raise StopAsyncIteration
        await Pause(("next", self.value))
        return self.value


async def task():
    values = []
    try:
        raise LookupError("owned")
    except LookupError as owned:
        async for value in Items():
            assert sys.exception() is owned
            values.append(value)
            await Pause(("body", value))
        assert sys.exception() is owned
    return values


coroutine = task()
ready = _thread.allocate_lock()
ready.acquire()
done = [_thread.allocate_lock(), _thread.allocate_lock()]
for lock in done:
    lock.acquire()
events = []
results = []
failures = []


def producer():
    try:
        events.append(coroutine.send(None))
        assert sys.exception() is None
        gc.collect()
    except BaseException as error:
        failures.append(type(error).__name__)
    finally:
        ready.release()
        done[0].release()


def consumer():
    ready.acquire()
    try:
        try:
            raise RuntimeError("ambient")
        except RuntimeError as ambient:
            while True:
                try:
                    events.append(coroutine.send(None))
                except StopIteration as result:
                    results.append(result.value)
                    break
                assert sys.exception() is ambient
                gc.collect()
            assert sys.exception() is ambient
    except BaseException as error:
        failures.append(type(error).__name__)
    finally:
        done[1].release()


_thread.start_new_thread(producer, ())
_thread.start_new_thread(consumer, ())
for lock in done:
    lock.acquire()
print(failures, events, results)
