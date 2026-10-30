import gc
import _thread


def drive(awaitable):
    iterator = awaitable.__await__()
    while True:
        try:
            print('pause', next(iterator))
        except StopIteration as result:
            return result.value


class AsyncRange:
    def __init__(self, end):
        self.index = 0
        self.end = end

    def __aiter__(self):
        return self

    async def __anext__(self):
        if self.index == self.end:
            raise StopAsyncIteration
        value = self.index
        self.index += 1
        return value


async def collect(iterator):
    values = []
    while True:
        try:
            values.append(await anext(iterator))
        except StopAsyncIteration:
            return values


async def with_default(iterator):
    sentinel = []
    values = []
    while True:
        value = await anext(iterator, sentinel)
        if value is sentinel:
            return values
        values.append(value)


async def agen():
    yield None
    yield 23


source = AsyncRange(3)
print('aiter identity', aiter(source) is source)
print('collected', drive(collect(aiter(source))))
print('default collection', drive(with_default(aiter(AsyncRange(2)))))
print('generator', drive(with_default(aiter(agen()))))
print('none default', drive(anext(AsyncRange(0), None)))
print('tuple default', drive(anext(AsyncRange(0), (1, 2))))


class Direct:
    def __init__(self, value):
        self.value = value

    def __anext__(self):
        return self.value


token = object()
print('raw result', anext(Direct(token)) is token)
print('no eager await check', type(anext(Direct(7), None)).__name__)


def error(label, func, *args, **kwargs):
    try:
        func(*args, **kwargs)
    except Exception as exc:
        print(label, type(exc).__name__)
    else:
        print(label, 'unexpected success')


class BadIterable:
    def __aiter__(self):
        return 9


class BrokenIterable:
    def __aiter__(self):
        raise LookupError('aiter callback')


class SynchronousEnd:
    def __anext__(self):
        raise StopAsyncIteration('synchronous')


class InstanceOnly:
    pass


instance_only = InstanceOnly()
instance_only.__aiter__ = lambda: source
instance_only.__anext__ = lambda: token
error('aiter missing', aiter, object())
error('aiter wrong result', aiter, BadIterable())
error('aiter callback', aiter, BrokenIterable())
error('instance aiter ignored', aiter, instance_only)
error('instance anext ignored', anext, instance_only)
error('anext missing', anext, [])
error('synchronous exhaustion', anext, SynchronousEnd(), 'not returned')
error('deferred invalid awaitable', drive, anext(Direct(7), None))
error('aiter no arguments', aiter)
error('aiter extra argument', aiter, source, None)
error('aiter keyword', aiter, async_iterable=source)
error('anext no arguments', anext)
error('anext extra argument', anext, source, None, None)
error('anext keyword', anext, source, default=None)


class BadAwait:
    def __await__(self):
        return 9


error('invalid await iterator', drive, anext(Direct(BadAwait()), None))


class Manual:
    def __init__(self):
        self.calls = 0

    def __await__(self):
        self.calls += 1
        return self

    def __next__(self):
        gc.collect()
        return 'next'

    def send(self, value):
        gc.collect()
        return value

    def throw(self, *args):
        print('throw args', len(args), args[0] is ValueError, args[1])
        return 'throw'

    def close(self, *args):
        print('close args', args)
        return 'close'


manual = Manual()
wrapped = anext(Direct(manual), 'default')
print('wrapper identity', wrapped.__await__() is wrapped, iter(wrapped) is wrapped)
print('manual next', next(wrapped))
print('manual send', wrapped.send('sent'))
print('manual throw', wrapped.throw(ValueError, 'injected'))
print('manual close', wrapped.close())
print('manual close args', wrapped.close(1, 2))
print('fresh await iterator', manual.calls)


class Exhausted(StopAsyncIteration):
    pass


class Outcome:
    def __init__(self, exception):
        self.exception = exception

    def __await__(self):
        return self

    def __next__(self):
        raise self.exception

    def send(self, value):
        raise self.exception

    def throw(self, *args):
        raise self.exception

    def close(self, *args):
        raise self.exception


def outcome(label, method, *args):
    try:
        method(*args)
    except StopIteration as result:
        print(label, 'returned', result.value)
    except Exception as exc:
        print(label, type(exc).__name__)


exhausted = anext(Direct(Outcome(Exhausted('end'))), ('default',))
outcome('exhausted next', next, exhausted)
outcome('exhausted send', exhausted.send, None)
outcome('exhausted throw', exhausted.throw, ValueError)
outcome('exhausted close', exhausted.close)
failed = anext(Direct(Outcome(ValueError('failure'))), 'unused')
outcome('failed next', next, failed)
outcome('failed send', failed.send, None)
outcome('failed throw', failed.throw, TypeError)
outcome('failed close', failed.close)
ordinary_stop = anext(Direct(Outcome(StopIteration('original'))), 'unused')
outcome('ordinary StopIteration', next, ordinary_stop)


class AwaitFailure:
    def __await__(self):
        raise Exhausted('await conversion')


class LookupFailure(Manual):
    @property
    def send(self):
        raise Exhausted('method lookup')


outcome('await conversion not suppressed', next, anext(Direct(AwaitFailure()), 'unused'))
lookup_failure = anext(Direct(LookupFailure()), 'lookup default')
outcome('method lookup converted', lookup_failure.send, None)


one_shot = anext(AsyncRange(1), 'unused')
print('one shot', drive(one_shot))
error('reuse', drive, one_shot)
closed = anext(AsyncRange(1), 'unused')
print('close coroutine', closed.close())
error('closed reuse', drive, closed)


class Hop:
    def __await__(self):
        yield 'handoff'
        gc.collect()


class RecoverIterator:
    async def __anext__(self):
        try:
            await Hop()
        except ValueError:
            return 51


recover = anext(RecoverIterator(), 'unused')
print('throw initial', recover.send(None))
outcome('throw into coroutine', recover.throw, ValueError('injected'))


class ClosingIterator:
    async def __anext__(self):
        try:
            await Hop()
        finally:
            print('close cleanup')


closing = anext(ClosingIterator(), 'unused')
print('close initial', closing.send(None))
print('close suspended', closing.close())
error('suspended close reuse', drive, closing)


class TransferIterator:
    async def __anext__(self):
        await Hop()
        raise StopAsyncIteration


def make_transfer():
    return anext(TransferIterator(), ['retained default'])


transfer = make_transfer()
print('thread initial', transfer.send(None))
gc.collect()
done = _thread.allocate_lock()
done.acquire()
results = []


def resume_elsewhere():
    try:
        transfer.send(None)
    except StopIteration as result:
        results.append(result.value)
    except BaseException as exc:
        results.append(type(exc).__name__)
    finally:
        done.release()


_thread.start_new_thread(resume_elsewhere, ())
done.acquire()
print('thread result', results)
