import _thread
import gc
import sys


def check(expected):
    assert sys.exception() is expected
    info = sys.exc_info()
    gc.collect()
    if expected is None:
        assert info == (None, None, None)
    else:
        assert info[0] is type(expected)
        assert info[1] is expected
        assert info[2] is expected.__traceback__


check(None)
outer = ValueError("outer")
inner = KeyError("inner")
try:
    raise outer
except ValueError:
    check(outer)
    try:
        raise inner
    except KeyError:
        check(inner)
    finally:
        check(outer)
    check(outer)
check(None)
print("nested and callee state")

pending = RuntimeError("pending")
try:
    try:
        raise pending
    finally:
        check(pending)
except RuntimeError:
    check(pending)
    try:
        raise
    except RuntimeError as reraised:
        assert reraised is pending
        check(pending)
    check(pending)
check(None)
print("finally and reraise state")


def donor_trace():
    try:
        raise LookupError("donor")
    except LookupError as exc:
        check(exc)
        return exc.__traceback__


try:
    raise outer
except ValueError:
    original_trace = outer.__traceback__
    assert original_trace is not None
    outer.__traceback__ = None
    check(outer)
    donor = donor_trace()
    check(outer)
    outer.__traceback__ = donor
    check(outer)
    assert sys.exc_info()[2] is donor
    outer.__traceback__ = original_trace
    check(outer)
check(None)
print("live traceback changes")


def inherited_generator():
    yield sys.exception()
    yield sys.exc_info()


inherited = inherited_generator()
try:
    raise outer
except ValueError:
    assert next(inherited) is outer
    check(outer)
check(None)
assert next(inherited) == (None, None, None)
inherited.close()
check(None)
print("generator borrowed state")


def suspended_generator():
    try:
        raise LookupError("generator")
    except LookupError as exc:
        check(exc)
        yield "generator pause"
        check(exc)
        return exc


class Pause:
    def __init__(self, exception):
        self.exception = exception

    def __await__(self):
        check(self.exception)
        yield "coroutine pause"
        check(self.exception)


async def suspended_coroutine():
    try:
        raise TypeError("coroutine")
    except TypeError as exc:
        check(exc)
        await Pause(exc)
        check(exc)
        return exc


generator = suspended_generator()
coroutine = suspended_coroutine()
ready = _thread.allocate_lock()
ready.acquire()
done = [_thread.allocate_lock(), _thread.allocate_lock()]
for lock in done:
    lock.acquire()
events = []
failures = []


def producer():
    try:
        check(None)
        try:
            raise ValueError("producer ambient")
        except ValueError as ambient:
            check(ambient)
            events.append(next(generator))
            check(ambient)
            events.append(coroutine.send(None))
            check(ambient)
        check(None)
    except BaseException as exc:
        failures.append(type(exc).__name__ + ": " + str(exc))
    finally:
        ready.release()
        done[0].release()


def consumer():
    ready.acquire()
    try:
        check(None)
        try:
            raise RuntimeError("consumer ambient")
        except RuntimeError as ambient:
            check(ambient)
            try:
                next(generator)
            except StopIteration as result:
                events.append(str(result.value))
                check(result)
            check(ambient)
            try:
                coroutine.send(None)
            except StopIteration as result:
                events.append(str(result.value))
                check(result)
            check(ambient)
        check(None)
    except BaseException as exc:
        failures.append(type(exc).__name__ + ": " + str(exc))
    finally:
        done[1].release()


_thread.start_new_thread(producer, ())
_thread.start_new_thread(consumer, ())
for lock in done:
    lock.acquire()
assert not failures, failures
check(None)
print("thread handoff", events)

for function in (sys.exception, sys.exc_info):
    try:
        function(1)
    except TypeError:
        pass
    else:
        raise AssertionError("unexpected positional argument accepted")
    try:
        function(value=None)
    except TypeError:
        pass
    else:
        raise AssertionError("unexpected keyword argument accepted")
check(None)
print("no exception and zero-argument signatures")
