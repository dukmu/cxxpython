import _thread
import gc
import sys

events = []
failures = []
results = []


class Pause:
    def __init__(self, label):
        self.label = label

    def __await__(self):
        yield self.label


class Manager:
    async def __aenter__(self):
        await Pause('enter')
        gc.collect()
        return self

    async def __aexit__(self, kind, error, trace):
        assert kind is ValueError and trace is not None
        assert sys.exc_info()[1] is error
        await Pause('exit')
        gc.collect()
        assert sys.exc_info()[1] is error
        return True


async def task():
    async with Manager() as manager:
        assert type(manager) is Manager
        await Pause('body')
        raise ValueError('body failure')
    return 42


coroutine = task()
ready = _thread.allocate_lock()
ready.acquire()
done = [_thread.allocate_lock(), _thread.allocate_lock()]
for lock in done:
    lock.acquire()


def producer():
    try:
        events.append(coroutine.send(None))
        gc.collect()
        events.append(coroutine.send(None))
    except BaseException as error:
        failures.append(type(error).__name__)
    finally:
        ready.release()
        done[0].release()


def consumer():
    ready.acquire()
    try:
        try:
            raise RuntimeError('ambient')
        except RuntimeError as ambient:
            assert sys.exc_info()[1] is ambient
            events.append(coroutine.send(None))
            assert sys.exc_info()[1] is ambient
            gc.collect()
            try:
                coroutine.send(None)
            except StopIteration as result:
                results.append(result.value)
            assert sys.exc_info()[1] is ambient
    except BaseException as error:
        failures.append(type(error).__name__)
    finally:
        done[1].release()


_thread.start_new_thread(producer, ())
_thread.start_new_thread(consumer, ())
for lock in done:
    lock.acquire()
print(failures, events, results)
